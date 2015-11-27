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

#include "CoeffsMatrix.h"
#include "CoeffsVector.h"
#include "ves_basisfunctions/BasisFunctions.h"

#include "tools/Tools.h"
#include "core/Value.h"
#include "tools/File.h"
#include "tools/Exception.h"
#include "tools/Random.h"
#include "tools/Communicator.h"

namespace PLMD{

CoeffsMatrix::CoeffsMatrix(
  const std::string& label,
  const std::vector<std::string>& dimension_labels,
  const std::vector<unsigned int>& indices_shape,
  Communicator& cc,
  const bool diagonal,
  const bool use_counter):
CounterBase(use_counter),
CoeffsBase(label,dimension_labels,indices_shape),
mycomm(cc),
diagonal_(diagonal),
output_fmt_("%30.16e")
{
  setupMatrix();
}


CoeffsMatrix::CoeffsMatrix(
  const std::string& label,
  std::vector<Value*>& args,
  std::vector<BasisFunctions*>& basisf,
  Communicator& cc,
  const bool diagonal,
  const bool use_counter):
CounterBase(use_counter),
CoeffsBase(label,args,basisf),
mycomm(cc),
diagonal_(diagonal),
output_fmt_("%30.16e")
{
  setupMatrix();
}


CoeffsMatrix::CoeffsMatrix(
  const std::string& label,
  CoeffsVector* coeffsVec,
  Communicator& cc,
  const bool diagonal,
  const bool use_counter):
CounterBase(use_counter),
CoeffsBase( *(static_cast<CoeffsBase*>(coeffsVec)) ),
mycomm(cc),
diagonal_(diagonal),
output_fmt_("%30.16e")
{
  setLabel(label);
  setDataLabel(label);
  setupMatrix();
}


void CoeffsMatrix::setupMatrix() {
  nrows_=numberOfCoeffs();
  ncolumns_=nrows_;
  if(diagonal_){
    size_=nrows_;
  }
  else{
    size_=(nrows_*nrows_-nrows_)/2+nrows_;
  }
  clear();
}


size_t CoeffsMatrix::getSize() const {
  return size_;
}


bool CoeffsMatrix::isDiagonal() const {
  return diagonal_;
}


void CoeffsMatrix::sumCommMPI() {
  mycomm.Sum(data);
}


void CoeffsMatrix::sumCommMPI(Communicator& cc) {
  cc.Sum(data);
}


void CoeffsMatrix::sumMultiSimCommMPI(Communicator& multi_sim_cc) {
  if(mycomm.Get_rank()==0){
    double nwalkers = (double) multi_sim_cc.Get_size();
    multi_sim_cc.Sum(data);
    scaleAllValues(1.0/nwalkers);
  }
  multi_sim_cc.Bcast(data,0);
}


size_t CoeffsMatrix::getMatrixIndex(const size_t index1, const size_t index2) const {
  size_t matrix_idx;
  plumed_dbg_assert(index1<nrows_);
  plumed_dbg_assert(index2<ncolumns_);
  if(diagonal_){
    // plumed_massert(index1==index2,"CoeffsMatrix: you trying to access a off-diagonal element of a diagonal coeffs matrix");
    matrix_idx=index1;
  }
  else if (index1<=index2){
    matrix_idx=index2+index1*(nrows_-1)-index1*(index1-1)/2;
  }
  else {
    matrix_idx=index1+index2*(nrows_-1)-index2*(index2-1)/2;
  }
  return matrix_idx;
}


void CoeffsMatrix::clear() {
  data.resize(getSize());
  for(size_t i=0; i<data.size(); i++){
    data[i]=0.0;
  }
}


double CoeffsMatrix::getValue(const size_t index1, const size_t index2) const {
  return data[getMatrixIndex(index1,index2)];
}


double CoeffsMatrix::getValue(const std::vector<unsigned int>& indices1, const std::vector<unsigned int>& indices2) const {
  return getValue(getIndex(indices1),getIndex(indices2));
}


void CoeffsMatrix::setValue(const size_t index1, const size_t index2, const double value) {
  data[getMatrixIndex(index1,index2)]=value;
}


void CoeffsMatrix::setValue(const std::vector<unsigned int>& indices1, const std::vector<unsigned int>& indices2, const double value) {
  setValue(getIndex(indices1),getIndex(indices2),value);
}


double& CoeffsMatrix::operator()(const size_t index1, const size_t index2) {
  return data[getMatrixIndex(index1,index2)];
}


const double& CoeffsMatrix::operator()(const size_t index1, const size_t index2) const {
  return data[getMatrixIndex(index1,index2)];
}


double& CoeffsMatrix::operator()(const std::vector<unsigned int>& indices1, const std::vector<unsigned int>& indices2) {
  return data[getMatrixIndex(getIndex(indices1),getIndex(indices2))];
}


const double& CoeffsMatrix::operator()(const std::vector<unsigned int>& indices1, const std::vector<unsigned int>& indices2) const {
  return data[getMatrixIndex(getIndex(indices1),getIndex(indices2))];
}


CoeffsVector operator*(const CoeffsMatrix& coeffs_matrix, const CoeffsVector& coeffs_vector) {
  CoeffsVector new_coeffs_vector(coeffs_vector);
  new_coeffs_vector.clear();
  plumed_massert(coeffs_vector.numberOfCoeffs()==coeffs_matrix.numberOfCoeffs(),"CoeffsMatrix and CoeffsVector are of the wrong size");
  size_t numcoeffs = coeffs_vector.numberOfCoeffs();
  if(coeffs_matrix.isDiagonal()){
    for(size_t i=0; i<numcoeffs; i++){
      new_coeffs_vector(i) = coeffs_matrix(i,i)*coeffs_vector(i);
    }
  }
  else{
    for(size_t i=0; i<numcoeffs; i++){
      for(size_t j=0; j<numcoeffs; j++){
        new_coeffs_vector(i) += coeffs_matrix(i,j)*coeffs_vector(j);
      }
    }
  }
  return new_coeffs_vector;
}


void CoeffsMatrix::addToValue(const size_t index1, const size_t index2, const double value) {
  data[getMatrixIndex(index1,index2)]+=value;
}


void CoeffsMatrix::addToValue(const std::vector<unsigned int>& indices1, const std::vector<unsigned int>& indices2, const double value) {
  addToValue(getIndex(indices1),getIndex(indices2),value);
}


void CoeffsMatrix::scaleAllValues(const double scalef) {
  for(size_t i=0; i<data.size(); i++){
    data[i]*=scalef;
  }
}


CoeffsMatrix& CoeffsMatrix::operator*=(const double scalef) {
  scaleAllValues(scalef);
  return *this;
}


CoeffsMatrix operator*(const double scalef, const CoeffsMatrix& coeffsmatrix) {
  return CoeffsMatrix(coeffsmatrix)*=scalef;
}


CoeffsMatrix operator*(const CoeffsMatrix& coeffsmatrix, const double scalef) {
  return scalef*coeffsmatrix;
}


CoeffsMatrix& CoeffsMatrix::operator*=(const CoeffsMatrix& other_coeffsmatrix) {
  plumed_massert(data.size()==other_coeffsmatrix.getSize(),"Coeffs matrices do not have the same size");
  for(size_t i=0; i<data.size(); i++){
    data[i]*=other_coeffsmatrix.data[i];
  }
  return *this;
}


CoeffsMatrix CoeffsMatrix::operator*(const CoeffsMatrix& other_coeffsmatrix) const {
  return CoeffsMatrix(*this)*=other_coeffsmatrix;
}


void CoeffsMatrix::setValues(const double value) {
  for(size_t i=0; i<data.size(); i++){
    data[i]=value;
  }
}


void CoeffsMatrix::setValues(const std::vector<double>& values) {
  plumed_massert( data.size()==values.size(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]=values[i];
  }
}


void CoeffsMatrix::setValues(const CoeffsMatrix& other_coeffsmatrix) {
  plumed_massert( data.size()==other_coeffsmatrix.getSize(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]=other_coeffsmatrix.data[i];
  }
}


CoeffsMatrix& CoeffsMatrix::operator=(const double value) {
  setValues(value);
  return *this;
}


CoeffsMatrix& CoeffsMatrix::operator=(const std::vector<double>& values) {
  setValues(values);
  return *this;
}


CoeffsMatrix& CoeffsMatrix::operator=(const CoeffsMatrix& other_coeffsmatrix) {
  setValues(other_coeffsmatrix);
  return *this;
}


CoeffsMatrix CoeffsMatrix::operator+() const {
  return *this;
}


CoeffsMatrix CoeffsMatrix::operator-() const {
  return CoeffsMatrix(*this)*=-1.0;
}


void CoeffsMatrix::addToValues(const double value) {
  for(size_t i=0; i<data.size(); i++){
    data[i]+=value;
  }
}


void CoeffsMatrix::addToValues(const std::vector<double>& values) {
  plumed_massert( data.size()==values.size(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]+=values[i];
  }
}


void CoeffsMatrix::addToValues(const CoeffsMatrix& other_coeffsmatrix) {
  plumed_massert( data.size()==other_coeffsmatrix.getSize(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]+=other_coeffsmatrix.data[i];
  }
}


void CoeffsMatrix::subtractFromValues(const double value) {
  for(size_t i=0; i<data.size(); i++){
    data[i]-=value;
  }
}


void CoeffsMatrix::subtractFromValues(const std::vector<double>& values) {
  plumed_massert( data.size()==values.size(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]-=values[i];
  }
}


void CoeffsMatrix::subtractFromValues(const CoeffsMatrix& other_coeffsmatrix) {
  plumed_massert( data.size()==other_coeffsmatrix.getSize(), "Incorrect size");
  for(size_t i=0; i<data.size(); i++){
    data[i]-=other_coeffsmatrix.data[i];
  }
}


CoeffsMatrix& CoeffsMatrix::operator+=(const double value) {
  addToValues(value);
  return *this;
}


CoeffsMatrix operator+(const double value, const CoeffsMatrix& coeffsmatrix) {
  return coeffsmatrix+value;
}


CoeffsMatrix operator+(const CoeffsMatrix& coeffsmatrix, const double value) {
  return CoeffsMatrix(coeffsmatrix)+=value;
}


CoeffsMatrix& CoeffsMatrix::operator+=(const std::vector<double>& values) {
  addToValues(values);
  return *this;
}


CoeffsMatrix operator+(const std::vector<double>& values, const CoeffsMatrix& coeffsmatrix) {
  return coeffsmatrix+values;
}


CoeffsMatrix operator+(const CoeffsMatrix& coeffsmatrix, const std::vector<double>& values) {
  return CoeffsMatrix(coeffsmatrix)+=values;
}


CoeffsMatrix& CoeffsMatrix::operator-=(const double value) {
  subtractFromValues(value);
  return *this;
}


CoeffsMatrix operator-(const double value, const CoeffsMatrix& coeffsmatrix) {
  return -1.0*coeffsmatrix+value;
}


CoeffsMatrix operator-(const CoeffsMatrix& coeffsmatrix, const double value) {
  return CoeffsMatrix(coeffsmatrix)-=value;
}


CoeffsMatrix& CoeffsMatrix::operator-=(const std::vector<double>& values) {
  subtractFromValues(values);
  return *this;
}


CoeffsMatrix operator-(const std::vector<double>& values, const CoeffsMatrix& coeffsmatrix) {
  return -1.0*coeffsmatrix+values;
}


CoeffsMatrix operator-(const CoeffsMatrix& coeffsmatrix, const std::vector<double>& values) {
  return CoeffsMatrix(coeffsmatrix)-=values;
}


CoeffsMatrix& CoeffsMatrix::operator+=(const CoeffsMatrix& other_coeffsmatrix) {
  addToValues(other_coeffsmatrix);
  return *this;
}


CoeffsMatrix CoeffsMatrix::operator+(const CoeffsMatrix& other_coeffsmatrix) const {
  return CoeffsMatrix(*this)+=other_coeffsmatrix;
}


CoeffsMatrix& CoeffsMatrix::operator-=(const CoeffsMatrix& other_coeffsmatrix) {
  subtractFromValues(other_coeffsmatrix);
  return *this;
}


CoeffsMatrix CoeffsMatrix::operator-(const CoeffsMatrix& other_coeffsmatrix) const {
  return CoeffsMatrix(*this)-=other_coeffsmatrix;
}


double CoeffsMatrix::getMinValue() const {
  double min_value=DBL_MAX;
  for(size_t i=0; i<data.size(); i++){
	  if(data[i]<min_value){
      min_value=data[i];
    }
  }
  return min_value;
}


double CoeffsMatrix::getMaxValue() const {
  double max_value=DBL_MIN;
  for(size_t i=0; i<data.size(); i++){
	  if(data[i]>max_value){
      max_value=data[i];
    }
  }
  return max_value;
}


void CoeffsMatrix::randomizeValuesGaussian(int randomSeed) {
  Random rnd;
  if (randomSeed<0){randomSeed = -randomSeed;}
  rnd.setSeed(-randomSeed);
  for(size_t i=0; i<data.size(); i++){
    data[i]=rnd.Gaussian();
  }
}


void CoeffsMatrix::writeToFile(OFile& ofile, const double current_time) {
  writeHeaderToFile(ofile, current_time);
  writeDataToFile(ofile);
}


void CoeffsMatrix::writeToFile(const std::string& filepath, const double current_time, const bool append_file) {
  OFile file;
  if(append_file){ file.enforceRestart(); }
  file.link(mycomm);
  file.open(filepath);
  writeToFile(file, current_time);
  file.close();
}


void CoeffsMatrix::writeMatrixInfoToFile(OFile& ofile) {
  std::string field_diagonal = "diagonal_matrix";
  ofile.addConstantField(field_diagonal).printField(field_diagonal,isDiagonal());
}


void CoeffsMatrix::writeHeaderToFile(OFile& ofile, const double current_time) {
  ofile.clearFields();
  if(current_time >= 0.0){writeTimeInfoToFile(ofile,current_time);}
  writeCounterInfoToFile(ofile);
  writeCoeffsInfoToFile(ofile);
  writeMatrixInfoToFile(ofile);
}


void CoeffsMatrix::writeDataToFile(OFile& ofile) {
  if(diagonal_){
    writeDataDiagonalToFile(ofile);
  }
  else{
    writeDataFullToFile(ofile);
  }
}


void CoeffsMatrix::writeDataDiagonalToFile(OFile& ofile) {
  //
  std::string field_indices_prefix = "idx_";
  std::string field_coeffs = getDataLabel();
  std::string field_index = "index";
  //
  std::string int_fmt = "%8d";
  std::string str_seperate = "#!-------------------";
  //
  char* s1 = new char[20];
  std::vector<unsigned int> indices(numberOfDimensions());
  std::vector<std::string> ilabels(numberOfDimensions());
  for(unsigned int k=0; k<numberOfDimensions(); k++){
    ilabels[k]=field_indices_prefix+getDimensionLabel(k);
  }
  //
  for(size_t i=0; i<numberOfCoeffs(); i++){
    indices=getIndices(i);
    for(unsigned int k=0; k<numberOfDimensions(); k++){
      sprintf(s1,int_fmt.c_str(),indices[k]);
      ofile.printField(ilabels[k],s1);
    }
    ofile.fmtField(" "+output_fmt_).printField(field_coeffs,getValue(i,i));
    sprintf(s1,int_fmt.c_str(),i); ofile.printField(field_index,s1);
    ofile.printField();
  }
  ofile.fmtField();
  // blank line between iterations to allow proper plotting with gnuplot
  ofile.printf("%s\n",str_seperate.c_str());
  ofile.printf("\n");
  ofile.printf("\n");
  delete [] s1;
}


void CoeffsMatrix::writeDataFullToFile(OFile& ofile) {
  //
  std::string field_index_row = "idx_row";
  std::string field_index_column = "idx_column";
  std::string field_coeffs = getDataLabel();
  //
  std::string int_fmt = "%8d";
  std::string str_seperate = "#!-------------------";
  //
  char* s1 = new char[20];
  //
  for(size_t i=0; i<nrows_; i++){
    for(size_t j=0; j<ncolumns_; j++){
      sprintf(s1,int_fmt.c_str(),i);
      ofile.printField(field_index_row,s1);
      sprintf(s1,int_fmt.c_str(),j);
      ofile.printField(field_index_column,s1);
      ofile.fmtField(" "+output_fmt_).printField(field_coeffs,getValue(i,j));
      ofile.printField();
    }
  }
  ofile.fmtField();
  // blank line between iterations to allow proper plotting with gnuplot
  ofile.printf("%s\n",str_seperate.c_str());
  ofile.printf("\n");
  ofile.printf("\n");
  delete [] s1;
}


}
