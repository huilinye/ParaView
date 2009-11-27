/*=========================================================================

  Program:   ParaView
  Module:    $RCSfile$

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#ifndef __vtkPVGUIPluginInterface_h
#define __vtkPVGUIPluginInterface_h

#include <QObjectList>

/// vtkPVGUIPluginInterface defines the interface required by GUI plugins. This
/// simply provides access to the GUI-component interfaces defined in this
/// plugin.
class vtkPVGUIPluginInterface 
{
public:
  virtual QObjectList interfaces() = 0;
};

#endif

