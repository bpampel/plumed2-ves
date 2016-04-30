/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2014,2015 The plumed team
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
#ifndef __PLUMED_gridtools_ContourFindingBase_h
#define __PLUMED_gridtools_ContourFindingBase_h

#include "ActionWithInputGrid.h"
#include "tools/RootFindingBase.h"

namespace PLMD {
namespace gridtools {

class ContourFindingBase : public ActionWithInputGrid {
private:
/// Stuff for output 
  OFile of;
  double lenunit;
  std::string fmt_xyz;
/// This is the object that does the root finding
  RootFindingBase<ContourFindingBase> mymin;
/// The data is stored in a grid
  vesselbase::StoreDataVessel* mydata;
protected:
/// Where you would like to find the contour
  double contour;
/// Find a contour along line specified by direction
  void findContour( const std::vector<double>& direction, std::vector<double>& point ) const ;
public:
  static void registerKeywords( Keywords& keys );
  explicit ContourFindingBase(const ActionOptions&ao);
/// Get the contour value
  double getDifferenceFromContour( const std::vector<double>& x, std::vector<double>& der ) const ;
/// Overwrite not needed stuff
  unsigned getNumberOfDerivatives(){ return 0; }
/// This is not periodic
  bool isPeriodic(){ return false; }
/// Number of quantities is the number of points in each point on the grid
  virtual unsigned getNumberOfQuantities() const { return 1 + ingrid->getDimension(); }
/// This does output if needs be
  void finishTaskSet();
};

inline
void ContourFindingBase::findContour( const std::vector<double>& direction, std::vector<double>& point ) const {
  mymin.linesearch( direction, point, &ContourFindingBase::getDifferenceFromContour );
}

inline
double ContourFindingBase::getDifferenceFromContour( const std::vector<double>& x, std::vector<double>& der ) const {
  return getFunctionValueAndDerivatives( x, der ) - contour;
}

}
}
#endif
