/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2011-2014 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "Optimizer.h"
#include "ves_tools/CoeffsVector.h"
#include "ves_tools/CoeffsMatrix.h"
#include "ves_biases/VesBias.h"

#include "tools/Exception.h"
#include "core/PlumedMain.h"
#include "core/ActionSet.h"
#include "tools/Communicator.h"
#include "tools/File.h"



namespace PLMD{

Optimizer::Optimizer(const ActionOptions&ao):
Action(ao),
ActionPilot(ao),
ActionWithValue(ao),
description_("Undefined"),
type_("Undefined"),
stepsizes_(0),
current_stepsizes(0),
fixed_stepsize_(true),
iter_counter(0),
use_hessian_(false),
diagonal_hessian_(true),
use_mwalkers_mpi_(false),
mwalkers_mpi_single_files_(true),
fname_prefix_(""),
coeffs_wstride_(100),
coeffsOFiles_(0),
gradient_wstride_(100),
gradientOFiles_(0),
hessian_wstride_(100),
hessianOFiles_(0),
nbiases_(0),
bias_pntrs(0),
ncoeffssets_(0),
coeffs_pntrs(0),
aux_coeffs_pntrs(0),
gradient_pntrs(0),
hessian_pntrs(0),
coeffs_mask_pntrs(0),
identical_coeffs_shape_(true)
{
  std::vector<std::string> bias_labels(0);
  parseVector("BIAS",bias_labels);
  plumed_massert(bias_labels.size()>0,"problem with BIAS keyword");
  nbiases_ = bias_labels.size();
  //
  bias_pntrs.resize(nbiases_);
  //
  for(unsigned int i=0; i<nbiases_; i++) {
    bias_pntrs[i]=plumed.getActionSet().selectWithLabel<bias::VesBias*>(bias_labels[i]);
    if(!bias_pntrs[i]){plumed_merror("VES bias "+bias_labels[i]+" does not exist. NOTE: the optimizer should always be defined AFTER the VES biases.");}
    //
    bias_pntrs[i]->linkOptimizer(this);
    //
    std::vector<CoeffsVector*> pntrs_coeffs = bias_pntrs[i]->getCoeffsPntrs();
    std::vector<CoeffsVector*> pntrs_gradient = bias_pntrs[i]->getGradientPntrs();
    plumed_massert(pntrs_coeffs.size()==pntrs_gradient.size(),"something wrong in the coefficients and gradient passed from VES bias");
    for(unsigned int k=0; k<pntrs_coeffs.size(); k++){
      plumed_massert(pntrs_coeffs[k] != NULL,"some coefficient is not linked correctly");
      plumed_massert(pntrs_gradient[k] != NULL,"some gradient is not linked correctly");
      coeffs_pntrs.push_back(pntrs_coeffs[k]);
      gradient_pntrs.push_back(pntrs_gradient[k]);
      CoeffsVector* aux_coeffs_tmp = new CoeffsVector(*pntrs_coeffs[k]);
      std::string aux_label = pntrs_coeffs[k]->getLabel();
      if(aux_label.find("coeffs")!=std::string::npos){
        aux_label.replace(aux_label.find("coeffs"), std::string("coeffs").length(), "aux_coeffs");
      }
      else {
        aux_label += "_aux";
      }
      aux_coeffs_tmp->setLabels(aux_label);
      aux_coeffs_pntrs.push_back(aux_coeffs_tmp);
      AuxCoeffs(i) = Coeffs(i);
    }
  }
  ncoeffssets_ = coeffs_pntrs.size();
  plumed_massert(aux_coeffs_pntrs.size()==ncoeffssets_,"problems in linking aux coefficients");
  plumed_massert(gradient_pntrs.size()==ncoeffssets_,"problems in linking gradients");


  //
  identical_coeffs_shape_ = true;
  for(unsigned int i=1; i<ncoeffssets_; i++) {
    if(!coeffs_pntrs[0]->sameShape(*coeffs_pntrs[i])){
      identical_coeffs_shape_ = false;
      break;
    }
  }
  //
  if(keywords.exists("STEPSIZE")){
    plumed_assert(!keywords.exists("INITIAL_STEPSIZE"));
    fixed_stepsize_=true;
    parseValues("STEPSIZE",stepsizes_);
    setCurrentStepSizes(stepsizes_);
  }
  if(keywords.exists("INITIAL_STEPSIZE")){
    plumed_assert(!keywords.exists("STEPSIZE"));
    fixed_stepsize_=false;
    parseValues("INITIAL_STEPSIZE",stepsizes_);
    setCurrentStepSizes(stepsizes_);
  }
  //
  if(ncoeffssets_==1){
    log.printf("  optimizing VES bias %s with label %s: \n",bias_pntrs[0]->getName().c_str(),bias_pntrs[0]->getLabel().c_str());
    log.printf("   KbT: %f\n",bias_pntrs[0]->getKbT());
    log.printf("  number of coefficients: %d\n",static_cast<int>(coeffs_pntrs[0]->numberOfCoeffs()));
    if(stepsizes_.size()>0){
      if(fixed_stepsize_){log.printf("  using a constant step size of %f\n",stepsizes_[0]);}
      else{log.printf("  using an initial step size of %f\n",stepsizes_[0]);}
    }
  }
  else {
    log.printf("  optimizing %d coefficent sets from following %d VES biases:\n",static_cast<int>(ncoeffssets_),static_cast<int>(nbiases_));
    for(unsigned int i=0; i<nbiases_; i++) {
      log.printf("   %s of type %s (KbT: %f) \n",bias_pntrs[i]->getLabel().c_str(),bias_pntrs[i]->getName().c_str(),bias_pntrs[i]->getKbT());
    }
    size_t tot_ncoeffs = 0;
    for(unsigned int i=0; i<ncoeffssets_; i++) {
      log.printf("  coefficient set %d: \n",static_cast<int>(i));
      log.printf("   used in bias %s (type %s)\n",coeffs_pntrs[i]->getPntrToAction()->getLabel().c_str(),coeffs_pntrs[i]->getPntrToAction()->getName().c_str());
      log.printf("   number of coefficients: %d\n",static_cast<int>(coeffs_pntrs[i]->numberOfCoeffs()));
      if(stepsizes_.size()>0){
        if(fixed_stepsize_){log.printf("   using a constant step size of %f\n",stepsizes_[i]);}
        else{log.printf("   using an initial step size of %f\n",stepsizes_[i]);}
      }
      tot_ncoeffs += coeffs_pntrs[i]->numberOfCoeffs();
    }
    log.printf("  total number of coefficients: %d\n",static_cast<int>(tot_ncoeffs));
    if(identical_coeffs_shape_){
      log.printf("  the indices shape is identical for all coefficient sets\n");
    }
    else{
      log.printf("  the indices shape differs between coefficient sets\n");
    }
  }

  //
  if(keywords.exists("FULL_HESSIAN")){
    bool full_hessian=false;
    parseFlag("FULL_HESSIAN",full_hessian);
    diagonal_hessian_ = !full_hessian;
  }
  //
  if(keywords.exists("MULTIPLE_WALKERS")){
    parseFlag("MULTIPLE_WALKERS",use_mwalkers_mpi_);
  }
  if(keywords.exists("MWALKERS_SEPERATE_FILES")){
    bool mw_seperate_files = false;
    parseFlag("MWALKERS_SEPERATE_FILES",mw_seperate_files);
    mwalkers_mpi_single_files_ = !mw_seperate_files;
  }

  if(comm.Get_rank()==0){
    if(use_mwalkers_mpi_ && multi_sim_comm.Get_size()==1){
      plumed_merror("using the MULTIPLE_WALKERS keyword does not make sense if running the MD code with a single replica");
    }
    if(use_mwalkers_mpi_ ){
      log.printf("  optimization performed using multiple walkers connected via MPI:\n");
      log.printf("   number of walkers: %d\n",static_cast<int>(multi_sim_comm.Get_size()));
      log.printf("   walker number: %d\n",static_cast<int>(multi_sim_comm.Get_rank()));
    }
  }

  if(ncoeffssets_>1){
    fname_prefix_="c-";
    parse("BIASID_SUFFIX",fname_prefix_);
    fname_prefix_ = "." + fname_prefix_;
  }
  else{
    fname_prefix_="";
    parse("BIASID_SUFFIX",fname_prefix_);
    if(fname_prefix_.size()>0){
      plumed_merror("BIASID_SUFFIX should only be given if optimizing multiple coefficent sets");
    }
  }

  if(keywords.exists("INITIAL_COEFFS")){
    std::vector<std::string> initial_coeffs_fnames;
    parseFilenames("INITIAL_COEFFS",initial_coeffs_fnames);
    if(initial_coeffs_fnames.size()>0){
      readCoeffsFromFiles(initial_coeffs_fnames);
    }
  }
  //


  std::vector<std::string> coeffs_fnames;
  parseFilenames("FILE",coeffs_fnames,"coeffs.data");

  std::string coeffs_wstride_tmpstr="";
  parse("OUTPUT_STRIDE",coeffs_wstride_tmpstr);
  if(coeffs_wstride_tmpstr!="OFF" && coeffs_wstride_tmpstr.size()>0){
    Tools::convert(coeffs_wstride_tmpstr,coeffs_wstride_);
  }
  if(coeffs_wstride_tmpstr=="OFF"){
    coeffs_fnames.clear();
  }
  setupOFiles(coeffs_fnames,coeffsOFiles_);
  for(unsigned int i=0; i<coeffsOFiles_.size();i++){
    coeffs_pntrs[i]->writeToFile(*coeffsOFiles_[i],aux_coeffs_pntrs[i],false,getTimeStep()*getStep());
  }

  if(coeffs_fnames.size()>0){
    if(ncoeffssets_==1){
      log.printf("  Coefficients will be written out to file %s every %d iterations\n",coeffsOFiles_[0]->getPath().c_str(),static_cast<int>(coeffs_wstride_));
    }
    else {
      log.printf("  Coefficients will be written out to the following files every %d iterations:\n",static_cast<int>(coeffs_wstride_));
      for(unsigned int i=0; i<coeffs_fnames.size(); i++){
        log.printf("   coefficient set %d: %s\n",static_cast<int>(i),coeffsOFiles_[i]->getPath().c_str());
      }
    }
  }
  else {
    log.printf("  Output of coefficients to file has been disabled\n");
  }


  std::vector<std::string> gradient_fnames;
  parseFilenames("GRADIENT_FILE",gradient_fnames);
  parse("GRADIENT_OUTPUT_STRIDE",gradient_wstride_);

  for(unsigned int i=0; i<gradient_fnames.size(); i++){
    plumed_massert(gradient_fnames[i]!=coeffs_fnames[i],"FILE and GRADIENT_FILE cannot be the same");
  }
  setupOFiles(gradient_fnames,gradientOFiles_);
  for(unsigned int i=0; i<gradientOFiles_.size(); i++){
    gradient_pntrs[i]->writeToFile(*gradientOFiles_[i],false,getTimeStep()*getStep());
  }

  if(gradient_fnames.size()>0){
    if(ncoeffssets_==1){
      log.printf("  Gradient will be written out to file %s every %d iterations\n",gradientOFiles_[0]->getPath().c_str(),static_cast<int>(gradient_wstride_));
    }
    else {
      log.printf("  Gradient will be written out to the following files every %d iterations:\n",static_cast<int>(gradient_wstride_));
      for(unsigned int i=0; i<gradient_fnames.size(); i++){
        log.printf("   coefficient set %d: %s\n",static_cast<int>(i),gradientOFiles_[i]->getPath().c_str());
      }
    }
  }


  if(keywords.exists("HESSIAN_FILE")){
    std::vector<std::string> hessian_fnames;
    parseFilenames("HESSIAN_FILE",hessian_fnames);

    for(unsigned int i=0; i<hessian_fnames.size(); i++){
      plumed_massert(hessian_fnames[i]!=coeffs_fnames[i],"FILE and HESSIAN_FILE cannot be the same");
      plumed_massert(hessian_fnames[i]!=gradient_fnames[i],"GRADIENT_FILE and HESSIAN_FILE cannot be the same");
    }
    setupOFiles(hessian_fnames,hessianOFiles_);

    if(hessian_fnames.size()>0){
      if(ncoeffssets_==1){
        log.printf("  Hessian will be written out to file %s every %d iterations\n",hessianOFiles_[0]->getPath().c_str(),static_cast<int>(hessian_wstride_));
      }
      else {
        log.printf("  Hessian will be written out to the following files every %d iterations:\n",static_cast<int>(hessian_wstride_));
        for(unsigned int i=0; i<hessian_fnames.size(); i++){
          log.printf("   coefficient set %d: %s\n",static_cast<int>(i),hessianOFiles_[i]->getPath().c_str());
        }
      }
    }
  }

  //
  if(keywords.exists("MASK_FILE")){
    std::vector<std::string> mask_fnames_in;
    parseVector("MASK_FILE",mask_fnames_in);
    if(mask_fnames_in.size()==1 && ncoeffssets_>1){
      if(identical_coeffs_shape_){mask_fnames_in.resize(ncoeffssets_,mask_fnames_in[0]);}
      else{plumed_merror("the coefficients indices shape differs between biases so you need to give a seperate file for each coefficient set\n");}
    }
    if(mask_fnames_in.size()>0 && mask_fnames_in.size()!=ncoeffssets_){
      plumed_merror("Error in MASK_FILE keyword: either give one value for all biases or a seperate value for each coefficient set");
    }

    coeffs_mask_pntrs.resize(ncoeffssets_);
    for(unsigned int i=0; i<ncoeffssets_; i++){
      coeffs_mask_pntrs[i] = new CoeffsVector(*coeffs_pntrs[i]);
      coeffs_mask_pntrs[i]->setLabels("mask");
      coeffs_mask_pntrs[i]->setValues(1.0);
      coeffs_mask_pntrs[i]->setOutputFmt("%f");
    }

    if(mask_fnames_in.size()>0){
      if(ncoeffssets_==1){
        size_t nread = coeffs_mask_pntrs[0]->readFromFile(mask_fnames_in[0],true,true);
        log.printf("  read %d values from mask file %s\n",static_cast<int>(nread),mask_fnames_in[0].c_str());
        size_t ndeactived = coeffs_mask_pntrs[0]->countValues(0.0);
        log.printf("  deactived optimization of %d coefficients\n",static_cast<int>(ndeactived));
      }
      else{
        for(unsigned int i=0; i<ncoeffssets_; i++){
          size_t nread = coeffs_mask_pntrs[i]->readFromFile(mask_fnames_in[i],true,true);
          log.printf("  mask for coefficent set %d:\n",static_cast<int>(i));
          log.printf("   read %d values from file %s\n",static_cast<int>(nread),mask_fnames_in[i].c_str());
          size_t ndeactived = coeffs_mask_pntrs[0]->countValues(0.0);
          log.printf("   deactived optimization of %d coefficients\n",static_cast<int>(ndeactived));
        }
      }
    }

    std::vector<std::string> mask_fnames_out;
    parseFilenames("OUTPUT_MASK_FILE",mask_fnames_out);

    for(unsigned int i=0; i<mask_fnames_out.size(); i++){
      if(mask_fnames_in.size()>0){
        plumed_massert(mask_fnames_out[i]!=mask_fnames_in[i],"MASK_FILE and OUTPUT_MASK_FILE cannot be the same");
      }
      OFile maskOFile;
      maskOFile.link(*this);
      if(use_mwalkers_mpi_ && mwalkers_mpi_single_files_){
        unsigned int r=0;
        if(comm.Get_rank()==0){r=multi_sim_comm.Get_rank();}
        comm.Bcast(r,0);
        if(r>0){mask_fnames_out[i]="/dev/null";}
        maskOFile.enforceSuffix("");
      }
      maskOFile.open(mask_fnames_out[i]);
      coeffs_mask_pntrs[i]->writeToFile(maskOFile,true,getTimeStep()*getStep());
      maskOFile.close();
    }
  }


  if(ncoeffssets_==1){
    log.printf("  Output Components:\n");
    log.printf(" ");
    addComponent("gradrms"); componentIsNotPeriodic("gradrms");
    log.printf(" ");
    addComponent("gradmax"); componentIsNotPeriodic("gradmax");
    if(!fixed_stepsize_){
      log.printf(" ");
      addComponent("stepsize"); componentIsNotPeriodic("stepsize");
    }
  }
  else {
    for(unsigned int i=0; i<ncoeffssets_; i++){
      log.printf("  Output Components for coefficent set %d:\n",static_cast<int>(i));
      std::string is=""; Tools::convert(i,is); is = "-" + is;
      log.printf(" ");
      addComponent("gradrms"+is); componentIsNotPeriodic("gradrms"+is);
      log.printf(" ");
      addComponent("gradmax"+is); componentIsNotPeriodic("gradmax"+is);
      if(!fixed_stepsize_){
        log.printf(" ");
        addComponent("stepsize"+is); componentIsNotPeriodic("stepsize"+is);
      }
    }
  }
}


Optimizer::~Optimizer() {
  for(unsigned int i=0; i<aux_coeffs_pntrs.size(); i++){
    delete aux_coeffs_pntrs[i];
  }
  aux_coeffs_pntrs.clear();
  for(unsigned int i=0; i<coeffsOFiles_.size(); i++){
    coeffsOFiles_[i]->close();
    delete coeffsOFiles_[i];
  }
  coeffsOFiles_.clear();
  for(unsigned int i=0; i<gradientOFiles_.size(); i++){
    gradientOFiles_[i]->close();
    delete gradientOFiles_[i];
  }
  gradientOFiles_.clear();
  for(unsigned int i=0; i<hessianOFiles_.size(); i++){
    hessianOFiles_[i]->close();
    delete hessianOFiles_[i];
  }
  hessianOFiles_.clear();
}


void Optimizer::registerKeywords( Keywords& keys ) {
  Action::registerKeywords(keys);
  ActionPilot::registerKeywords(keys);
  ActionWithValue::registerKeywords(keys);
  // Default always active keywords
  keys.add("compulsory","BIAS","the label of the VES bias to be optimized");
  keys.add("compulsory","STRIDE","the frequency of updating the coefficients");
  keys.add("compulsory","FILE","COEFFS","the name of output file for the coefficients");
  keys.add("compulsory","OUTPUT_STRIDE","100","how often the coefficients should be written to file. This parameter is given as the number of iterations.");
  keys.add("optional","BIASID_SUFFIX","suffix to add to the filename given in FILE to identfy the bias, should only be given if a single filename is given in FILE when optimizing multiple biases.");
  //
  keys.add("optional","INITIAL_COEFFS","the name(s) of file(s) with the initial coefficents");
  // Hidden keywords to output the gradient to a file.
  keys.add("hidden","GRADIENT_FILE","the name of output file for the gradient");
  keys.add("hidden","GRADIENT_OUTPUT_STRIDE","how often the gradient should be written to file. This parameter is given as the number of bias iterations. It is by default 100 if GRADIENT_FILE is specficed");
  // Either use a fixed stepsize (useFixedStepSizeKeywords) or changing stepsize (useDynamicsStepSizeKeywords)
  keys.reserve("compulsory","STEPSIZE","the step size used for the optimization");
  keys.reserve("compulsory","INITIAL_STEPSIZE","the initial step size used for the optimization");
  // Keywords related to the Hessian, actived with the useHessianKeywords function
  keys.reserveFlag("FULL_HESSIAN",false,"if the full Hessian matrix should be used for the optimization, otherwise only the diagonal Hessian is used");
  keys.reserve("hidden","HESSIAN_FILE","the name of output file for the Hessian");
  keys.reserve("hidden","HESSIAN_OUTPUT_STRIDE","how often the Hessian should be written to file. This parameter is given as the number of bias iterations. It is by default 100 if HESSIAN_FILE is specficed");
  // Keywords related to the multiple walkers, actived with the useMultipleWalkersKeywords function
  keys.reserveFlag("MULTIPLE_WALKERS",false,"if optimization is to be performed using multiple walkers connected via MPI");
  keys.reserveFlag("MWALKERS_SEPERATE_FILES",false,"DEBUG OPTION: if seperate files should be outputted to file when using MPI multiple walkers");
  // Keywords related to the mask file, actived with the useMaskKeywords function
  keys.reserve("optional","MASK_FILE","read in a mask file which allows one to employ different step sizes for different coefficents and/or deactive the optimization of certain coefficients (by putting values of 0.0). One can write out the resulting mask by using the OUTPUT_MASK_FILE keyword.");
  keys.reserve("optional","OUTPUT_MASK_FILE","Name of the file to write out the mask resulting from using the MASK_FILE keyword. Can also be used to generate a template mask file.");
  //
  // Components that are always active
  keys.addOutputComponent("gradrms","default","the root mean square value of the coefficent gradient. For multiple biases this component is labeled using the number of the bias as gradrms-#.");
  keys.addOutputComponent("gradmax","default","the largest absolute value of the coefficent gradient. For multiple biases this component is labeled using the number of the bias as gradmax-#.");
  ActionWithValue::useCustomisableComponents(keys);
  // keys.addOutputComponent("gradmaxidx","default","the index of the maximum absolute value of the gradient");
}


void Optimizer::useHessianKeywords(Keywords& keys) {
  keys.use("FULL_HESSIAN");
  keys.use("HESSIAN_FILE");
  keys.use("HESSIAN_OUTPUT_STRIDE");
}


void Optimizer::useMultipleWalkersKeywords(Keywords& keys) {
  keys.use("MULTIPLE_WALKERS");
  keys.use("MWALKERS_SEPERATE_FILES");
}


void Optimizer::useFixedStepSizeKeywords(Keywords& keys) {
  keys.use("STEPSIZE");
}


void Optimizer::useDynamicStepSizeKeywords(Keywords& keys) {
  keys.use("INITIAL_STEPSIZE");
  keys.addOutputComponent("stepsize","default","the current value of step size used to update the coefficients. For multiple biases this component is labeled using the number of the bias as stepsize-#.");
}


void Optimizer::useMaskKeywords(Keywords& keys) {
  keys.use("MASK_FILE");
  keys.use("OUTPUT_MASK_FILE");
}


void Optimizer::turnOnHessian() {
  plumed_massert(hessian_pntrs.size()==0,"turnOnHessian() should only be run during initialization");
  use_hessian_=true;
  hessian_pntrs.clear();
  for(unsigned int i=0; i<nbiases_; i++){
    std::vector<CoeffsMatrix*> pntrs_hessian = enableHessian(bias_pntrs[i],diagonal_hessian_);
    for(unsigned int k=0; k<pntrs_hessian.size(); k++){
      hessian_pntrs.push_back(pntrs_hessian[k]);
    }
  }
  plumed_massert(hessian_pntrs.size()==ncoeffssets_,"problems in linking Hessians");
  if(diagonal_hessian_){
    log.printf("  Optimization performed using diagonal Hessian matrix\n");
  }
  else {
    log.printf("  Optimization performed using full Hessian matrix\n");
  }
  //
  for(unsigned int i=0; i<hessianOFiles_.size(); i++){
    hessian_pntrs[i]->writeToFile(*hessianOFiles_[i],getTimeStep()*getStep());
  }
}


void Optimizer::turnOffHessian() {
  use_hessian_=false;
  for(unsigned int i=0; i<nbiases_; i++){
    bias_pntrs[i]->disableHessian();
  }
  hessian_pntrs.clear();
  for(unsigned int i=0; i<hessianOFiles_.size(); i++){
    hessianOFiles_[i]->close();
    delete hessianOFiles_[i];
  }
  hessianOFiles_.clear();
}


std::vector<CoeffsMatrix*> Optimizer::enableHessian(bias::VesBias* bias_pntr_in, const bool diagonal_hessian) {
  plumed_massert(use_hessian_,"the Hessian should not be used");
  bias_pntr_in->enableHessian(diagonal_hessian);
  std::vector<CoeffsMatrix*> hessian_pntrs_out = bias_pntr_in->getHessianPntrs();
  for(unsigned int k=0; k<hessian_pntrs_out.size(); k++){
    plumed_massert(hessian_pntrs_out[k] != NULL,"Hessian is needed but not linked correctly");
  }
  return hessian_pntrs_out;
}


// CoeffsMatrix* Optimizer::switchToDiagonalHessian(bias::VesBias* bias_pntr_in) {
//   plumed_massert(use_hessian_,"it does not make sense to switch to diagonal Hessian if it Hessian is not used");
//   diagonal_hessian_=true;
//   bias_pntr_in->enableHessian(diagonal_hessian_);
//   CoeffsMatrix* hessian_pntr_out = bias_pntr_in->getHessianPntr();
//   plumed_massert(hessian_pntr_out != NULL,"Hessian is needed but not linked correctly");
//   //
//   log.printf("  %s (with label %s): switching to a diagonal Hessian for VES bias %s (with label %s) at time  %f\n",getName().c_str(),getLabel().c_str(),bias_pntr_in->getName().c_str(),bias_pntr_in->getLabel().c_str(),getTime());
//   return hessian_pntr_out;
// }


// CoeffsMatrix* Optimizer::switchToFullHessian(bias::VesBias* bias_pntr_in) {
//   plumed_massert(use_hessian_,"it does not make sense to switch to diagonal Hessian if it Hessian is not used");
//   diagonal_hessian_=false;
//   bias_pntr_in->enableHessian(diagonal_hessian_);
//   CoeffsMatrix* hessian_pntr_out = bias_pntr_in->getHessianPntr();
//   plumed_massert(hessian_pntr_out != NULL,"Hessian is needed but not linked correctly");
//   //
//   log.printf("  %s (with label %s): switching to a diagonal Hessian for VES bias %s (with label %s) at time  %f\n",getName().c_str(),getLabel().c_str(),bias_pntr_in->getName().c_str(),bias_pntr_in->getLabel().c_str(),getTime());
//   return hessian_pntr_out;
// }


void Optimizer::update() {
  if(onStep() && getStep()!=0){
    for(unsigned int i=0; i<nbiases_; i++){
      bias_pntrs[i]->updateGradientAndHessian();
    }
    for(unsigned int i=0; i<ncoeffssets_; i++){
      if(use_mwalkers_mpi_){
        gradient_pntrs[i]->sumMultiSimCommMPI(multi_sim_comm);
        if(use_hessian_){hessian_pntrs[i]->sumMultiSimCommMPI(multi_sim_comm);}
      }
      coeffsUpdate(i);
      coeffs_pntrs[i]->increaseCounter();
      aux_coeffs_pntrs[i]->increaseCounter();
      gradient_pntrs[i]->increaseCounter();
      if(use_hessian_){hessian_pntrs[i]->increaseCounter();}
    }
    increaseIterationCounter();
    updateOutputComponents();
    writeOutputFiles();
  }
}


void Optimizer::updateOutputComponents() {
  if(ncoeffssets_==1){
    if(!fixed_stepsize_){
      getPntrToComponent("stepsize")->set( getCurrentStepSize(0) );
    }
    getPntrToComponent("gradrms")->set( gradient_pntrs[0]->getRMS() );
    size_t gradient_maxabs_idx=0;
    getPntrToComponent("gradmax")->set( gradient_pntrs[0]->getMaxAbsValue(gradient_maxabs_idx) );
  }
  else {
    for(unsigned int i=0; i<ncoeffssets_; i++){
      std::string is=""; Tools::convert(i,is); is = "-" + is;
      if(!fixed_stepsize_){
        getPntrToComponent("stepsize"+is)->set( getCurrentStepSize(i) );
      }
      getPntrToComponent("gradrms"+is)->set( gradient_pntrs[i]->getRMS() );
      size_t gradient_maxabs_idx=0;
      getPntrToComponent("gradmax"+is)->set( gradient_pntrs[i]->getMaxAbsValue(gradient_maxabs_idx) );
    }
  }
}


void Optimizer::writeOutputFiles() {
  for(unsigned int i=0; i<ncoeffssets_; i++){
    if(coeffsOFiles_.size()>0 && iter_counter%coeffs_wstride_==0){
      coeffs_pntrs[i]->writeToFile(*coeffsOFiles_[i],aux_coeffs_pntrs[i],false,getTimeStep()*getStep());
    }
    if(gradientOFiles_.size()>0 && iter_counter%gradient_wstride_==0){
      gradient_pntrs[i]->writeToFile(*gradientOFiles_[i],false,getTimeStep()*getStep());
    }
    if(hessianOFiles_.size()>0 && iter_counter%hessian_wstride_==0){
      hessian_pntrs[i]->writeToFile(*hessianOFiles_[i],getTimeStep()*getStep());
    }
  }
}


void Optimizer::turnOffCoeffsOutputFiles() {
  for(unsigned int i=0; i<coeffsOFiles_.size(); i++){
    coeffsOFiles_[i]->close();
    delete coeffsOFiles_[i];
  }
  coeffsOFiles_.clear();
}


void Optimizer::writeOutputFiles(const unsigned int coeffs_id) {
  if(coeffsOFiles_.size()>0 && iter_counter%coeffs_wstride_==0){
    coeffs_pntrs[coeffs_id]->writeToFile(*coeffsOFiles_[coeffs_id],aux_coeffs_pntrs[coeffs_id],false,getTimeStep()*getStep());
  }
  if(gradientOFiles_.size()>0 && iter_counter%gradient_wstride_==0){
    gradient_pntrs[coeffs_id]->writeToFile(*gradientOFiles_[coeffs_id],false,getTimeStep()*getStep());
  }
  if(hessianOFiles_.size()>0 && iter_counter%hessian_wstride_==0){
    hessian_pntrs[coeffs_id]->writeToFile(*hessianOFiles_[coeffs_id],getTimeStep()*getStep());
  }
}


void Optimizer::setupOFiles(std::vector<std::string>& fnames, std::vector<OFile*>& OFiles) {
  plumed_assert(ncoeffssets_>0);
  OFiles.resize(fnames.size(),NULL);
  for(unsigned int i=0; i<fnames.size();i++){
    OFiles[i] = new OFile();
    OFiles[i]->link(*this);
    if(use_mwalkers_mpi_ && mwalkers_mpi_single_files_){
      unsigned int r=0;
      if(comm.Get_rank()==0){r=multi_sim_comm.Get_rank();}
      comm.Bcast(r,0);
      if(r>0){fnames[i]="/dev/null";}
      OFiles[i]->enforceSuffix("");
    }
    OFiles[i]->open(fnames[i]);
    OFiles[i]->setHeavyFlush();
  }
}


void Optimizer::readCoeffsFromFiles(const std::vector<std::string>& fnames) {
  plumed_assert(ncoeffssets_>0);
  plumed_assert(fnames.size()==ncoeffssets_);
  if(ncoeffssets_==1){
    log.printf("  Reading initial coefficents from file ");
  }
  else{
    log.printf("  Reading initial coefficents from files:\n");
  }
  for(unsigned int i=0; i<ncoeffssets_; i++){
    IFile ifile;
    ifile.link(*this);
    if(use_mwalkers_mpi_ && mwalkers_mpi_single_files_){
      ifile.enforceSuffix("");
    }
    ifile.open(fnames[i]);
    if(!ifile.FieldExist(coeffs_pntrs[i]->getDataLabel())){
      std::string error_msg = "Reading of initial coefficents: no field with name " + coeffs_pntrs[i]->getDataLabel() + "in file " + fnames[i] + "\n";
      plumed_merror(error_msg);
    }
    size_t ncoeffs_read = coeffs_pntrs[i]->readFromFile(ifile,false,false);
    if(ncoeffssets_==1){
      log.printf("%s (read %zu of %zu values)\n", ifile.getPath().c_str(),ncoeffs_read,coeffs_pntrs[i]->numberOfCoeffs());
    }
    else{
      log.printf("   coefficent set %u: %s (read %zu of %zu values)\n",i,ifile.getPath().c_str(),ncoeffs_read,coeffs_pntrs[i]->numberOfCoeffs());
    }
    ifile.close();
    ifile.open(fnames[i]);
    if(ifile.FieldExist(aux_coeffs_pntrs[i]->getDataLabel())){
      size_t nauxcoeffs_read = aux_coeffs_pntrs[i]->readFromFile(ifile,false,false);
    }
    else{
      // to avoid warning
      ifile.allowIgnoredFields();
      ifile.scanField();
      AuxCoeffs(i) = Coeffs(i);
    }
    ifile.close();
  }
}


void Optimizer::addCoeffsIDsToFilenames(std::vector<std::string>& fnames, std::string& fname_prefix) {
  if(ncoeffssets_==1){return;}
  //
  if(fnames.size()==1){
    fnames.resize(ncoeffssets_,fnames[0]);
  }
  plumed_assert(fnames.size()==ncoeffssets_);
  //
  for(unsigned int i=0; i<ncoeffssets_; i++){
    std::string is=""; Tools::convert(i,is);
    fnames[i] = FileBase::appendSuffix(fnames[i],fname_prefix_+is);
  }
}


}
