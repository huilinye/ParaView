/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$
  
-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include <vtkArrayData.h>
#include <vtkArrayPrint.h>
#include <vtkMatricizeArray.h>
#include <vtkSmartPointer.h>
#include <vtkSparseArray.h>

#include <vtksys/ios/iostream>
#include <vtksys/stl/stdexcept>

#define test_expression(expression) \
{ \
  if(!(expression)) \
    throw vtkstd::runtime_error("Expression failed: " #expression); \
}

int ArrayMatricizeArray(int vtkNotUsed(argc), char *vtkNotUsed(argv)[])
{
  try
    {
    // Create an array ...
    vtkSmartPointer<vtkSparseArray<double> > array = vtkSmartPointer<vtkSparseArray<double> >::New();
    array->Resize(vtkArrayExtents(2, 2, 2));

    double value = 0;
    const vtkArrayExtents extents = array->GetExtents();
    for(int i = 0; i != extents[0]; ++i)
      {
      for(int j = 0; j != extents[1]; ++j)
        {
        for(int k = 0; k != extents[2]; ++k)
          {
          array->AddValue(vtkArrayCoordinates(i, j, k), value++);
          }
        }
      }

    cout << "array source:\n";
    vtkPrintCoordinateFormat(cout, array.GetPointer());

    // Create an array data object to hold it ...
    vtkSmartPointer<vtkArrayData> array_data = vtkSmartPointer<vtkArrayData>::New();
    array_data->AddArray(array);

    // Matricize it ...
    vtkSmartPointer<vtkMatricizeArray> matricize = vtkSmartPointer<vtkMatricizeArray>::New();
    matricize->SetInput(array_data);
    matricize->SetSliceDimension(0);
    matricize->Update();

    vtkSparseArray<double>* const matricized_array = vtkSparseArray<double>::SafeDownCast(
      matricize->GetOutput()->GetArray(static_cast<vtkIdType>(0)));
    test_expression(matricized_array);

    cout << "matricize output:\n";
    vtkPrintCoordinateFormat(cout, matricized_array);

    test_expression(matricized_array->GetValue(vtkArrayCoordinates(0, 0)) == 0);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(0, 1)) == 1);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(0, 2)) == 2);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(0, 3)) == 3);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(1, 0)) == 4);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(1, 1)) == 5);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(1, 2)) == 6);
    test_expression(matricized_array->GetValue(vtkArrayCoordinates(1, 3)) == 7);

    return 0;
    }
  catch(vtkstd::exception& e)
    {
    cout << e.what() << endl;
    return 1;
    }
}

