/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2015-2016 The ves-code team
   (see the PEOPLE-VES file at the root of the distribution for a list of names)

   See http://www.ves-code.org for more information.

   This file is part of ves-code, version 1.

   ves-code is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   ves-code is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with ves-code.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "BasisFunctions.h"
#include "ves_targetdistributions/TargetDistribution.h"
#include "ves_targetdistributions/TargetDistributionRegister.h"
#include "ves_tools/CoeffsVector.h"

#include "core/ActionRegister.h"
#include "core/ActionSet.h"
#include "core/PlumedMain.h"
#include "tools/File.h"
#include "tools/Grid.h"



// using namespace std;

namespace PLMD{
namespace generic{

//+PLUMEDOC FUNCTION DUMP_BASISFUNCTIONS
/*

*/
//+ENDPLUMEDOC


class DumpBasisFunctions :
  public Action
{
  std::vector<BasisFunctions*> bf_pntrs;
public:
  explicit DumpBasisFunctions(const ActionOptions&);
  TargetDistribution* setupTargetDistPntr(std::string keyword) const;
  void calculate(){}
  void apply(){}
  static void registerKeywords(Keywords& keys);
};


PLUMED_REGISTER_ACTION(DumpBasisFunctions,"DUMP_BASISFUNCTIONS")

void DumpBasisFunctions::registerKeywords(Keywords& keys){
  Action::registerKeywords(keys);
  keys.add("compulsory","BASIS_SET","the label of the basis set that you want to use");
  keys.add("optional","GRID_BINS","the number of bins used for the grid. The default value is 1000.");
  keys.add("optional","FILE_VALUES","filename of the file on which the basis function values are written. By default it is LABEL.values.data.");
  keys.add("optional","FILE_DERIVS","filename of the file on which the basis function derivatives are written. By default it is LABEL.derivs.data.");
  keys.add("optional","FILE_TARGETDIST_AVERAGES","filename of the file on which the averages over the target distributions are written. By default it is LABEL.targetdist-averages.data.");
  keys.add("optional","FILE_TARGETDIST","filename of the files on which the target distributions are written. By default it is LABEL.targetdist-#.data.");
  keys.add("numbered","TARGET_DISTRIBUTION","the target distribution to be used.");
  keys.addFlag("IGNORE_PERIODICITY",false,"if the periodicity of the basis functions should be ignored.");
}

DumpBasisFunctions::DumpBasisFunctions(const ActionOptions&ao):
Action(ao),
bf_pntrs(1)
{
  std::string basisset_label="";
  parse("BASIS_SET",basisset_label);
  bf_pntrs[0]=plumed.getActionSet().selectWithLabel<BasisFunctions*>(basisset_label);
  plumed_massert(bf_pntrs[0]!=NULL,"basis function "+basisset_label+" does not exist. NOTE: the basis functions should always be defined BEFORE the DUMP_BASISFUNCTIONS action.");

  unsigned int grid_bins = 1000;
  parse("GRID_BINS",grid_bins);

  std::string fname_values = bf_pntrs[0]->getLabel()+".values.data";
  parse("FILE_VALUES",fname_values);
  std::string fname_derives = bf_pntrs[0]->getLabel()+".derivs.data";
  parse("FILE_DERIVS",fname_derives);
  std::string fname_targetdist_aver = bf_pntrs[0]->getLabel()+".targetdist-averages.data";
  parse("FILE_TARGETDIST_AVERAGES",fname_targetdist_aver);
  std::string fname_targetdist = bf_pntrs[0]->getLabel()+".targetdist-.data";
  parse("FILE_TARGETDIST",fname_targetdist);

  bool ignore_periodicity = false;
  parseFlag("IGNORE_PERIODICITY",ignore_periodicity);

  std::vector<std::string> targetdist_keywords;
  std::string str_ps="";
  for(int i=1;;i++){
    if(!parseNumbered("TARGET_DISTRIBUTION",i,str_ps)){break;}
    targetdist_keywords.push_back(str_ps);
  }
  checkRead();
  //
  OFile ofile_values;
  ofile_values.link(*this);
  ofile_values.open(fname_values);
  OFile ofile_derivs;
  ofile_derivs.link(*this);
  ofile_derivs.open(fname_derives);
  bf_pntrs[0]->writeBasisFunctionsToFile(ofile_values,ofile_derivs,grid_bins,ignore_periodicity);
  ofile_values.close();
  ofile_derivs.close();
  //
  std::vector<std::string> min(1); min[0]=bf_pntrs[0]->intervalMinStr();
  std::vector<std::string> max(1); max[0]=bf_pntrs[0]->intervalMaxStr();
  std::vector<unsigned int> nbins(1); nbins[0]=grid_bins;
  std::vector<Value*> args(1);
  args[0]= new Value(NULL,"arg",false);
  if(bf_pntrs[0]->arePeriodic() && !ignore_periodicity){
    args[0]->setDomain(bf_pntrs[0]->intervalMinStr(),bf_pntrs[0]->intervalMaxStr());
  }
  else {
    args[0]->setNotPeriodic();
  }

  for(unsigned int i=0; i<targetdist_keywords.size(); i++){
    std::string is; Tools::convert(i+1,is);
    TargetDistribution* targetdist_pntr = setupTargetDistPntr(targetdist_keywords[i]);
    std::vector<double> bf_integrals = bf_pntrs[0]->getTargetDistributionIntegrals(targetdist_pntr);
    CoeffsVector targetdist_averages = CoeffsVector("aver.targetdist-"+is,args,bf_pntrs,comm,false);
    targetdist_averages.setValues(bf_integrals);
    targetdist_averages.writeToFile(fname_targetdist_aver,true,true,this);
    Grid ps_grid = Grid("targetdist-"+is,args,min,max,nbins,false,false);
    if(targetdist_pntr!=NULL){
      std::string fname = FileBase::appendSuffix(fname_targetdist,is);
      targetdist_pntr->calculateDistributionOnGrid(&ps_grid);
      OFile ofile;
      ofile.link(*this);
      ofile.enforceBackup();
      ofile.open(fname);
      ps_grid.writeToFile(ofile);
      ofile.close();
    }
    delete targetdist_pntr;
  }
  delete args[0]; args.clear();



}


TargetDistribution* DumpBasisFunctions::setupTargetDistPntr(std::string keyword) const {
  std::vector<std::string> words = Tools::getWords(keyword);
  TargetDistribution* pntr = NULL;
  if(words[0]=="UNIFORM"){
    pntr = NULL;
  }
  else{
    pntr = targetDistributionRegister().create(words);
    plumed_massert(pntr->getDimension()==1,"the target distribution must be one dimensional");
  }
  return pntr;
}







}
}
