// -*- c++ -*-
/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

// .NAME vtkNetCDFReader
//
// .SECTION Description
//
// A superclass for reading netCDF files.  Subclass add conventions to the
// reader.  This class just outputs data into a multi block data set with a
// vtkImageData at each block.  A block is created for each variable except that
// variables with matching dimensions will be placed in the same block.

#ifndef __vtkNetCDFReader_h
#define __vtkNetCDFReader_h

#include "vtkDataObjectAlgorithm.h"

#include "vtkSmartPointer.h"    // For ivars


class vtkDataArraySelection;
class vtkDataSet;
class vtkDoubleArray;
class vtkIntArray;
class vtkStdString;

class VTK_IO_EXPORT vtkNetCDFReader : public vtkDataObjectAlgorithm
{
public:
  vtkTypeRevisionMacro(vtkNetCDFReader, vtkDataObjectAlgorithm);
  static vtkNetCDFReader *New();
  virtual void PrintSelf(ostream &os, vtkIndent indent);

  virtual void SetFileName(const char *filename);
  vtkGetStringMacro(FileName);

  // Description:
  // Update the meta data from the current file.  Automatically called
  // during the RequestInformation pipeline update stage.
  int UpdateMetaData();

//   // Description:
//   // Get the data array selection tables used to configure which variables to
//   // load.
//   vtkGetObjectMacro(VariableArraySelection, vtkDataArraySelection);

  // Description:
  // Variable array selection.
  virtual int GetNumberOfVariableArrays();
  virtual const char *GetVariableArrayName(int idx);
  virtual int GetVariableArrayStatus(const char *name);
  virtual void SetVariableArrayStatus(const char *name, int status);

protected:
  vtkNetCDFReader();
  ~vtkNetCDFReader();

  char *FileName;
  vtkTimeStamp FileNameMTime;
  vtkTimeStamp MetaDataMTime;

//BTX
  // Description:
  // The dimension ids of the arrays being loaded into the data.
  vtkSmartPointer<vtkIntArray> LoadingDimensions;

  vtkSmartPointer<vtkDataArraySelection> VariableArraySelection;
//ETX

  virtual int RequestDataObject(vtkInformation *request,
                                vtkInformationVector **inputVector,
                                vtkInformationVector *outputVector);

  virtual int RequestInformation(vtkInformation *request,
                                 vtkInformationVector **inputVector,
                                 vtkInformationVector *outputVector);

  virtual int RequestData(vtkInformation *request,
                          vtkInformationVector **inputVector,
                          vtkInformationVector *outputVector);

  // Description:
  // Callback registered with the VariableArraySelection.
  static void SelectionModifiedCallback(vtkObject *caller, unsigned long eid,
                                        void *clientdata, void *calldata);

  // Description:
  // Convenience function for getting a string that describes a set of
  // dimensions.
  vtkStdString DescribeDimensions(int ncFD, const int *dimIds, int numDims);

  // Description:
  // Reads meta data and populates ivars.  Returns 1 on success, 0 on failure.
  virtual int ReadMetaData(int ncFD);

  // Description:
  // Determines whether the given variable is a time dimension.  The default
  // implementation bases the decision on the name of the variable.  Subclasses
  // should override this function if there is a more specific way to identify
  // the time variable.  This method is always called after ReadMetaData for
  // a given file.
  virtual int IsTimeDimension(int ncFD, int dimId);

//BTX
  // Description:
  // Given a dimension already determined to be a time dimension (via a call to
  // IsTimeDimension) returns an array with time values.  The default
  // implementation just uses the tiem index for the time value.  Subclasses
  // should override this function if there is a convention that identifies time
  // values.  This method returns 0 on error, 1 otherwise.
  virtual vtkSmartPointer<vtkDoubleArray> GetTimeValues(int ncFD, int dimId);
//ETX

  // Description:
  // Load the variable at the given time into the given data set.  Return 1
  // on success and 0 on failure.
  virtual int LoadVariable(int ncFD, const char *varName, double time,
                           vtkDataSet *output);

private:
  vtkNetCDFReader(const vtkNetCDFReader &);     // Not implemented
  void operator=(const vtkNetCDFReader &);      // Not implemented
};

#endif //__vtkNetCDFReader_h
