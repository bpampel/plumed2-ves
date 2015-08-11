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
#include <vector>
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <cfloat>

#include "CoeffsVector.h"
#include "tools/Tools.h"
#include "core/Value.h"
#include "tools/File.h"
#include "tools/Exception.h"
#include "BasisFunctions.h"
#include "tools/Random.h"

namespace PLMD{

CoeffsVector::CoeffsVector(const std::string& coeffs_label, const std::string& coeffs_type,
        const std::vector<std::string>& dimension_labels,
        const std::vector<unsigned int>& ncoeffs_per_dimension,
        const bool use_aux_coeffs, const bool use_counter){
  Init(coeffs_label, coeffs_type, dimension_labels, ncoeffs_per_dimension, use_aux_coeffs, use_counter);
  std::string description_prefix="c";
  setupCoeffsDescriptions(description_prefix);
}


CoeffsVector::CoeffsVector(const std::string& coeffs_label,
        std::vector<Value*> args,
        std::vector<BasisFunctions*> basisf,
        const bool use_aux_coeffs, const bool use_counter){
  plumed_massert(args.size()==basisf.size(),"number of arguments do not match number of basis functions");
  std::string coeffs_type="LinearBasisFunctionCoeffs";
  unsigned int dim=args.size();
  std::vector<std::string>dimension_labels;
  std::vector<unsigned int> ncoeffs_per_dimension;
  dimension_labels.resize(dim); ncoeffs_per_dimension.resize(dim);
  //
  for(unsigned int i=0;i<dim;i++){
    dimension_labels[i]=args[i]->getName();
    ncoeffs_per_dimension[i]=basisf[i]->getNumberOfBasisFunctions();
  }
  Init(coeffs_label, coeffs_type, dimension_labels, ncoeffs_per_dimension, use_aux_coeffs, use_counter);
  setupBasisFunctionsInfo(basisf);
  isbasisfcoeffs_=true;
}


void CoeffsVector::Init(const std::string& coeffs_label, const std::string& coeffs_type,
        const std::vector<std::string>& dimension_labels,
        const std::vector<unsigned int>& ncoeffs_per_dimension,
        const bool use_aux_coeffs, const bool use_counter){
  isbasisfcoeffs_=false;
  fmt_="%30.16e";
  plumed_massert(ncoeffs_per_dimension.size()==dimension_labels.size(),"Coeffs: dimensions of vectors in Init(...) don't match");
  dimension_=ncoeffs_per_dimension.size();
  dimension_labels_=dimension_labels;
  ncoeffs_per_dimension_=ncoeffs_per_dimension;
  coeffs_label_=coeffs_label;
  coeffs_type_=coeffs_type;
  usecounter_=use_counter;
  useaux_=use_aux_coeffs;
  ncoeffs_total_=1;
  for(unsigned int i=0;i<dimension_;i++){ncoeffs_total_*=ncoeffs_per_dimension_[i];}
  coeffs_descriptions_.resize(ncoeffs_total_);
  if(usecounter_){resetCounter();}
  clear();
}


void CoeffsVector::clearMain(){
  data.resize(ncoeffs_total_);
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]=0.0;}
}


void CoeffsVector::clearAux(){
  aux_data.resize(ncoeffs_total_);
  for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]=0.0;}
}


void CoeffsVector::clear(){
  clearMain();
  if(useaux_){clearAux();}
}


std::string CoeffsVector::getLabel() const {return coeffs_label_;}


std::string CoeffsVector::getType() const {return coeffs_type_;}


bool CoeffsVector::isBasisFunctionCoeffs() const {return isbasisfcoeffs_;}


bool CoeffsVector::hasAuxCoeffs() const {return useaux_;}


bool CoeffsVector::hasCounter() const {return usecounter_;}


double CoeffsVector::getValue(const index_t index)const{
  plumed_dbg_assert(index<ncoeffs_total_);
  return data[index];
}

double CoeffsVector::getValue(const std::vector<unsigned int>& indices)const{
  return getValue(getIndex(indices));
}


double CoeffsVector::getAuxValue(const index_t index)const{
  plumed_dbg_assert(index<ncoeffs_total_ && useaux_);
  return aux_data[index];
}


double CoeffsVector::getAuxValue(const std::vector<unsigned int>& indices)const{
  return getAuxValue(getIndex(indices));
}


void CoeffsVector::setValue(const index_t index, const double value){
  plumed_dbg_assert(index<ncoeffs_total_);
  data[index]=value;
}


void CoeffsVector::setValue(const std::vector<unsigned int>& indices, const double value){
  setValue(getIndex(indices),value);
}


void CoeffsVector::setAuxValue(const index_t index, const double value){
  plumed_dbg_assert(index<ncoeffs_total_ && useaux_);
  aux_data[index]=value;
}


void CoeffsVector::setAuxValue(const std::vector<unsigned int>& indices, const double value){
  setAuxValue(getIndex(indices),value);
}


void CoeffsVector::setValueAndAux(const index_t index, const double main_value, const double aux_value)
{
  plumed_dbg_assert(index<ncoeffs_total_ && useaux_);
  data[index]=main_value;
  aux_data[index]=aux_value;
}


void CoeffsVector::setValueAndAux(const std::vector<unsigned int>& indices, const double main_value, const double aux_value){
  setValueAndAux(getIndex(indices),main_value,aux_value);
}


void CoeffsVector::addToValue(const index_t index, const double value){
  plumed_dbg_assert(index<ncoeffs_total_);
  data[index]+=value;
}


void CoeffsVector::addToValue(const std::vector<unsigned int>& indices, const double value){
  addToValue(getIndex(indices),value);
}


void CoeffsVector::addToAuxValue(const index_t index, const double value){
  plumed_dbg_assert(index<ncoeffs_total_ && useaux_);
  aux_data[index]+=value;
}


void CoeffsVector::addToAuxValue(const std::vector<unsigned int>& indices, const double value){
  addToAuxValue(getIndex(indices),value);
}


void CoeffsVector::scaleAllValues(const double scalef ){
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]*=scalef;}
  if(useaux_){for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]*=scalef;}}
}


void CoeffsVector::scaleOnlyMainValues(const double scalef ){
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]*=scalef;}
}


void CoeffsVector::scaleOnlyAuxValues(const double scalef ){
  if(useaux_){for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]*=scalef;}}
}


void CoeffsVector::setValues(const double value){
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]=value;}
}


void CoeffsVector::setAuxValues(const double value){
  if(useaux_){for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]=value;}}
}


void CoeffsVector::addToValues(const double value){
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]+=value;}
}


void CoeffsVector::addToAuxValues(const double value){
  if(useaux_){for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]+=value;}}
}


void CoeffsVector::setAuxEqualToMain(){
  if(useaux_){for(index_t i=0;i<ncoeffs_total_;i++){aux_data[i]=data[i];}}
}


void CoeffsVector::setFromOtherCoeffs(CoeffsVector* other_coeffs){
  plumed_massert(ncoeffs_total_==other_coeffs->getSize(),"Coeffs do not have same number of elements");
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]=other_coeffs->getValue(i);}
}


void CoeffsVector::setFromOtherCoeffs(CoeffsVector* other_coeffs,const double scalef){
  plumed_massert(ncoeffs_total_==other_coeffs->getSize(),"Coeffs do not have same number of elements");
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]=scalef*other_coeffs->getValue(i);}
}


void CoeffsVector::addFromOtherCoeffs(CoeffsVector* other_coeffs){
  plumed_massert(ncoeffs_total_==other_coeffs->getSize(),"Coeffs do not have same number of elements");
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]+=other_coeffs->getValue(i);}
}


void CoeffsVector::addFromOtherCoeffs(CoeffsVector* other_coeffs, const double scalef){
  plumed_massert(ncoeffs_total_==other_coeffs->getSize(),"Coeffs do not have same number of elements");
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]+=scalef*other_coeffs->getValue(i);}
}


void CoeffsVector::randomizeCoeffs(){
  Random rnd;
  for(index_t i=0;i<ncoeffs_total_;i++){data[i]=rnd.Gaussian();}
}


void CoeffsVector::writeHeader(OFile& ofile){
  ofile.addConstantField("label").printField("label",coeffs_label_);
  ofile.addConstantField("type").printField("type",coeffs_type_);
  ofile.addConstantField("ncoeffs_total").printField("ncoeffs_total",(int) ncoeffs_total_);
  if(isbasisfcoeffs_){
    for(unsigned int i=0;i<dimension_;++i){
      // ofile.addConstantField(dimension_labels_[i]+"_bf_type");
      // ofile.printField(dimension_labels_[i]+"_bf_type",basisf_type_[i]);
      // ofile.addConstantField(dimension_labels_[i]+"_bf_order");
      // ofile.printField(dimension_labels_[i]+"_bf_order",(int) basisf_order_[i]);
      // ofile.addConstantField(dimension_labels_[i]+"_bf_min");
      // ofile.printField(dimension_labels_[i]+"_bf_min",basisf_min_[i]);
      // ofile.addConstantField(dimension_labels_[i]+"_bf_max");
      // ofile.printField(dimension_labels_[i]+"_bf_max",basisf_max_[i]);
      ofile.addConstantField(dimension_labels_[i]+"_bf_keywords");
      ofile.printField(dimension_labels_[i]+"_bf_keywords","{"+basisf_keywords_[i]+"}");
    }
  }
 for(unsigned int i=0;i<dimension_;++i){
   ofile.addConstantField(dimension_labels_[i]+"_ncoeffs");
   ofile.printField(dimension_labels_[i]+"_ncoeffs",(int) ncoeffs_per_dimension_[i]);
 }
 if(usecounter_){ofile.addConstantField("iteration").printField("iteration",(int) counter);}
}


void CoeffsVector::writeToFile(OFile& ofile, const bool print_description){
  std::string int_fmt="%8d";
  std::string str_seperate="#!-------------------";
  std::string ilabels_prefix="idx_";
  char* s1 = new char[20];
  std::vector<unsigned int> indices(dimension_);
  std::vector<std::string> ilabels(dimension_);
  for(unsigned int k=0; k<dimension_; k++){ilabels[k]=ilabels_prefix+dimension_labels_[k];}
  //
  writeHeader(ofile);
  for(index_t i=0;i<ncoeffs_total_;i++){
    indices=getIndices(i);
    for(unsigned int k=0; k<dimension_; k++){
      sprintf(s1,int_fmt.c_str(),indices[k]); ofile.printField(ilabels[k],s1);
    }
    ofile.fmtField(" "+fmt_).printField("coeff",data[i]);
    if(useaux_){ofile.fmtField(" "+fmt_).printField("aux_coeff",aux_data[i]);}
    sprintf(s1,int_fmt.c_str(),i); ofile.printField("index",s1);
    if(print_description){ofile.printField("description","  " +coeffs_descriptions_[i]);}
    ofile.printField();
  }
  ofile.fmtField();
  // blank line between iterations to allow proper plotting with gnuplot
  ofile.printf("%s\n",str_seperate.c_str());
  ofile.printf("\n");
  ofile.printf("\n");
  delete [] s1;
}


void CoeffsVector::writeToFile(const std::string& filepath, const bool print_description, const bool append_file){
  OFile file;
  if(append_file){file.enforceRestart();}
  file.open(filepath);
  writeToFile(file,print_description);
}


unsigned int CoeffsVector::readFromFile(IFile& ifile, const bool ignore_missing_coeffs){
 // ifile.allowIgnoredFields();
  std::string ilabels_prefix="idx_";
  std::vector<std::string> ilabels(dimension_);
  for(unsigned int k=0; k<dimension_; k++){ilabels[k]=ilabels_prefix+dimension_labels_[k];}
  // start reading header
  int int_tmp;
  // label
  std::string coeffs_label_f;
  ifile.scanField("label",coeffs_label_f);
  // type
  std::string coeffs_type_f;
  ifile.scanField("type",coeffs_type_f);
  // total number of coeffs
  ifile.scanField("ncoeffs_total",int_tmp);
  index_t ncoeffs_total_f=(index_t) int_tmp;
  // basis function keywords
  if(isbasisfcoeffs_){
    std::vector<std::string>  basisf_keywords_f(dimension_);
    for(unsigned int k=0; k<dimension_; k++){
      ifile.scanField(dimension_labels_[k]+"_bf_keywords",basisf_keywords_f[k]);
    }
  }
  // number of coeffs per dimension
  std::vector<unsigned int> ncoeffs_per_dimension_f(dimension_);
  for(unsigned int k=0; k<dimension_; k++){
    ifile.scanField(dimension_labels_[k]+"_ncoeffs",int_tmp);
    ncoeffs_per_dimension_f[k]=int_tmp;
  }
  // counter
  if(usecounter_){
    ifile.scanField("iteration",int_tmp);
    unsigned int counter_f=(unsigned int) int_tmp;
  }
  // reading of header finished
  std::vector<unsigned int> indices(dimension_);
  double coeff_tmp=0.0;
  std::string str_tmp;
  unsigned int ncoeffs_read=0;
  // read coeffs
  while(ifile.scanField("coeff",coeff_tmp)){
    int idx_tmp;
    for(unsigned int k=0; k<dimension_; k++){
      ifile.scanField(ilabels[k],idx_tmp);
      indices[k] = (unsigned int) idx_tmp;
    }
    data[getIndex(indices)] = coeff_tmp;
    if(useaux_){
      ifile.scanField("aux_coeff",coeff_tmp);
      aux_data[getIndex(indices)] = coeff_tmp;
    }
    ifile.scanField("index",idx_tmp);
    if(getIndex(indices)!=idx_tmp){
      std::string is1; Tools::convert(idx_tmp,is1);
      std::string msg="ERROR: problem with indices at index " + is1 + " when reading coefficients from file";
      plumed_merror(msg);
    }
    if(ifile.FieldExist("description")){ifile.scanField("description",str_tmp);}
    //
    ifile.scanField();
    ncoeffs_read++;
  }
  // checks on the coeffs read
  if(!ignore_missing_coeffs && ncoeffs_read < ncoeffs_total_){plumed_merror("ERROR: missing coefficients when reading from file");}
  if(ncoeffs_read > ncoeffs_total_){plumed_merror("something wrong in the coefficients file, perhaps multiple entries");}
  //
  return ncoeffs_read;
}


unsigned int CoeffsVector::readFromFile(const std::string& filepath, const bool ignore_missing_coeffs){
  IFile file; file.open(filepath);
  unsigned int ncoeffs_read=readFromFile(file,ignore_missing_coeffs);
  return ncoeffs_read;
}


CoeffsVector* CoeffsVector::createFromFile(IFile& ifile, const bool ignore_missing_coeffs){
  CoeffsVector* coeffs_ptr=NULL;
  // Find labels and number dimensions
  std::vector<std::string> fields;
  std::vector<std::string> dimension_labels_f;
  ifile.scanFieldList(fields);
  for(unsigned int i=0;i<fields.size();i++){
    if(fields[i].substr(0,4)=="idx_"){dimension_labels_f.push_back(fields[i].substr(4));}
  }
  unsigned int dim=dimension_labels_f.size();
  // start reading header
  int int_tmp;
  // label
  std::string coeffs_label_f;
  ifile.scanField("label",coeffs_label_f);
  std::string coeffs_type_f;
  ifile.scanField("type",coeffs_type_f);
  // basis function keywords
  bool isbasisfcoeffs_f=false;
  if(ifile.FieldExist(dimension_labels_f[0]+"_bf_keywords")){isbasisfcoeffs_f=true;}
  std::vector<std::string>  basisf_keywords_f(dim);
  if(isbasisfcoeffs_f){
    for(unsigned int k=0; k<dim; k++){
      ifile.scanField(dimension_labels_f[k]+"_bf_keywords",basisf_keywords_f[k]);
    }
  }
  // number of coeffs per dimension
  std::vector<unsigned int> ncoeffs_per_dimension_f(dim);
  for(unsigned int k=0; k<dim; k++){
    ifile.scanField(dimension_labels_f[k]+"_ncoeffs",int_tmp);
    ncoeffs_per_dimension_f[k]=int_tmp;
  }
  // counter
  bool use_counter_f=false;
  if(ifile.FieldExist("iteration")){use_counter_f=true;}
  bool use_aux_coeffs_f=false;
  if(ifile.FieldExist("aux_coeff")){use_aux_coeffs_f=true;}

  coeffs_ptr = new CoeffsVector(coeffs_label_f, coeffs_type_f, dimension_labels_f, ncoeffs_per_dimension_f, use_aux_coeffs_f, use_counter_f);
  if(isbasisfcoeffs_f){coeffs_ptr->setupBasisFunctionFromFile(basisf_keywords_f);}
  coeffs_ptr->readFromFile(ifile,ignore_missing_coeffs);
  return coeffs_ptr;
}


CoeffsVector* CoeffsVector::createFromFile(const std::string& filepath, const bool ignore_missing_coeffs){
  IFile file; file.open(filepath);
  CoeffsVector* coeffs_ptr=createFromFile(file,ignore_missing_coeffs);
  return coeffs_ptr;
}


std::vector<std::string> CoeffsVector::getBasisFunctionKeywordsFromFile(IFile& ifile)
{
  // Find labels and number dimensions
  std::vector<std::string> fields;
  std::vector<std::string> dimension_labels_f;
  ifile.allowIgnoredFields();
  ifile.scanFieldList(fields);
  for(unsigned int i=0;i<fields.size();i++){
    if(fields[i].substr(0,4)=="idx_"){dimension_labels_f.push_back(fields[i].substr(4));}
  }
  unsigned int dim=dimension_labels_f.size();

  bool isbasisfcoeffs_f=false;
  if(ifile.FieldExist(dimension_labels_f[0]+"_bf_keywords")){isbasisfcoeffs_f=true;}
  std::vector<std::string>  basisf_keywords_f;
  if(isbasisfcoeffs_f){
    basisf_keywords_f.resize(dim);
    for(unsigned int k=0; k<dim; k++){
      ifile.scanField(dimension_labels_f[k]+"_bf_keywords",basisf_keywords_f[k]);
    }
  }
  ifile.scanField();
  return basisf_keywords_f;
}


std::vector<std::string> CoeffsVector::getBasisFunctionKeywordsFromFile(const std::string& filepath){
  IFile file; file.open(filepath);
  std::vector<std::string> basisf_keywords_f=getBasisFunctionKeywordsFromFile(file);
  return basisf_keywords_f;
}


void CoeffsVector::resetCounter(){counter=0;}


void CoeffsVector::increaseCounter(){counter=+1;}


void CoeffsVector::addToCounter(unsigned int value){counter=+value;}


void CoeffsVector::setCounter(unsigned int value){counter=value;}


unsigned int CoeffsVector::getCounter() const {return counter;}


void CoeffsVector::setupBasisFunctionsInfo(std::vector<BasisFunctions*> basisf){
  plumed_massert(basisf.size()==dimension_,"setupBasisFunctionsInfo: wrong number of basis functions given.");
  basisf_type_.resize(dimension_);
  basisf_order_.resize(dimension_);
  basisf_size_.resize(dimension_);
  basisf_min_.resize(dimension_);
  basisf_max_.resize(dimension_);
  basisf_keywords_.resize(dimension_);
  for(unsigned int k=0;k<dimension_;k++){
    basisf_type_[k]=basisf[k]->getType();
    basisf_order_[k]=basisf[k]->getOrder();
    basisf_size_[k]=basisf[k]->getNumberOfBasisFunctions();
    basisf_min_[k]=basisf[k]->intervalMin();
    basisf_max_[k]=basisf[k]->intervalMax();
    basisf_keywords_[k]=basisf[k]->getKeywordString();
  }
  for(unsigned int i=0;i<ncoeffs_total_;i++){
    std::vector<unsigned int> indices=getIndices(i);
    std::string desc;
    desc=basisf[0]->getBasisFunctionDescription(indices[0]);
    for(unsigned int k=1;k<dimension_;k++){desc+="*"+basisf[k]->getBasisFunctionDescription(indices[k]);}
    coeffs_descriptions_[i]=desc;
  }
}


void CoeffsVector::setupBasisFunctionFromFile(const std::vector<std::string>& basisf_keywords_f){
  basisf_type_.resize(dimension_);
  basisf_order_.resize(dimension_);
  basisf_size_.resize(dimension_);
  basisf_min_.resize(dimension_);
  basisf_max_.resize(dimension_);
  basisf_keywords_.resize(dimension_);
  for(unsigned int k=0;k<dimension_;k++){
    basisf_type_[k]="";
    basisf_order_[k]=0;
    basisf_size_[k]=ncoeffs_per_dimension_[k];
    basisf_min_[k]=0.0;
    basisf_max_[k]=0.0;
    basisf_keywords_[k]=basisf_keywords_f[k];
  }
  isbasisfcoeffs_=true;
}


}
