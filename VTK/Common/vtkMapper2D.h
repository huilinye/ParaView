/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Thanks:    Thanks to Matt Turek who developed this class.

Copyright (c) 1993-1998 Ken Martin, Will Schroeder, Bill Lorensen.

This software is copyrighted by Ken Martin, Will Schroeder and Bill Lorensen.
The following terms apply to all files associated with the software unless
explicitly disclaimed in individual files. This copyright specifically does
not apply to the related textbook "The Visualization Toolkit" ISBN
013199837-4 published by Prentice Hall which is covered by its own copyright.

The authors hereby grant permission to use, copy, and distribute this
software and its documentation for any purpose, provided that existing
copyright notices are retained in all copies and that this notice is included
verbatim in any distributions. Additionally, the authors grant permission to
modify this software and its documentation for any purpose, provided that
such modifications are not distributed without the explicit consent of the
authors and that existing copyright notices are retained in all copies. Some
of the algorithms implemented by this software are patented, observe all
applicable patent law.

IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF,
EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON AN
"AS IS" BASIS, AND THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.


=========================================================================*/
// .NAME vtkMapper2D
// .SECTION Description
// vtkMapper2D is an abstract class which defines the interface for objects
// which render two dimensional actors (vtkActor2D).

// .SECTION See Also
// vtkActor2D

#ifndef __vtkMapper2D_h
#define __vtkMapper2D_h

#include "vtkReferenceCount.h"
#include "vtkViewport.h"
#include "vtkWindow.h"
#include "vtkActor2D.h"
// #include "vtkImager.h"

#define XMIN 0
#define YMIN 1
#define XMAX 2
#define YMAX 3

class VTK_EXPORT vtkMapper2D : public vtkReferenceCount
{
public:
  void PrintSelf(ostream& os, vtkIndent indent);
  virtual void Render(vtkViewport* viewport, vtkActor2D* actor) = 0;
  void GetActorClipSize(vtkViewport* viewport, vtkActor2D* actor, int* clipWidth, int* clipHeight);
  void GetViewportClipSize(vtkViewport* viewport, int* clipWidth, int* clipHeight);
protected:


};

#endif



