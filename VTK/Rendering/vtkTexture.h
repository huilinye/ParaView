/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$


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
// .NAME vtkTexture - handles properties associated with a texture map
// .SECTION Description
// vtkTexture is an object that handles loading and binding of texture
// maps. It obtains its data from an input structured points dataset type.
// Thus you can create visualization pipelines to read, process, and 
// construct textures. Note that textures will only work if texture
// coordinates are also defined, and if the rendering system supports 
// texture.
//
// Instances of vtkTexture are associated with actors via the actor's
// SetTexture() method. Actors can share texture maps (this is encouraged
// to save memory resources.) 

// .SECTION Caveats
// Currently only 2D texture maps are supported, even though the data pipeline
// supports 1,2, and 3D texture coordinates. 
// 
// Some renderers such as OpenGL require that the texture map dimensions are
// a power of two in each direction. Other renderers may have similar
// (ridiculous) restrictions, so be careful out there... (Note: a recent change
// to vtk allows use of non-power of two texture maps in OpenGL. The texture is
// automatically resampled to a power of two in one or more directions.)

// .SECTION See Also
// vtkActor vtkRenderer vtkOpenGLTexture

#ifndef __vtkTexture_h
#define __vtkTexture_h

#include "vtkObject.h"
#include "vtkStructuredPoints.h"
#include "vtkLookupTable.h"
#include "vtkScalars.h"
#include "vtkImageToStructuredPoints.h"

class vtkRenderer;
class vtkWindow;

class VTK_EXPORT vtkTexture : public vtkObject
{
public:
  vtkTexture();
  ~vtkTexture();
  static vtkTexture *New();
  const char *GetClassName() {return "vtkTexture";};
  void PrintSelf(ostream& os, vtkIndent indent);

  // Description:
  // Renders a texture map. It first checks the object's modified time
  // to make sure the texture maps Input is valid, then it invokes the 
  // Load() method.
  virtual void Render(vtkRenderer *ren);

  // Description:
  // Release any graphics resources that are being consumed by this texture.
  // The parameter window could be used to determine which graphic
  // resources to release.
  virtual void ReleaseGraphicsResources(vtkWindow *) {};

  // Description:
  // Abstract interface to renderer. Each concrete subclass of 
  // vtkTexture will load its data into graphics system in response 
  // to this method invocation.
  virtual void Load(vtkRenderer *) {};

  // Description:
  // Turn on/off the repetition of the texture map when the texture
  // coords extend beyond the [0,1] range.
  vtkGetMacro(Repeat,int);
  vtkSetMacro(Repeat,int);
  vtkBooleanMacro(Repeat,int);

  // Description:
  // Turn on/off linear interpolation of the texture map when rendering.
  vtkGetMacro(Interpolate,int);
  vtkSetMacro(Interpolate,int);
  vtkBooleanMacro(Interpolate,int);

  // Description:
  // Turn on/off the mapping of color scalars through the lookup table.
  // The default is Off. If Off, unsigned char scalars will be used
  // directly as texture. If On, scalars will be mapped through the
  // lookup table to generate 4-component unsigned char scalars.
  // This ivar does not affect other scalars like unsigned short, float,
  // etc. These scalars are always mapped through lookup tables.
  vtkGetMacro(MapColorScalarsThroughLookupTable,int);
  vtkSetMacro(MapColorScalarsThroughLookupTable,int);
  vtkBooleanMacro(MapColorScalarsThroughLookupTable,int);

  // Description:
  // Specify the data for the texture map.
  vtkSetObjectMacro(Input,vtkStructuredPoints);
  vtkGetObjectMacro(Input,vtkStructuredPoints);
  void SetInput(vtkImageData *cache)
    {vtkImageToStructuredPoints *tmp = cache->MakeImageToStructuredPoints();
    this->SetInput(tmp->GetOutput()); tmp->Delete();}
  
  // Description:
  // Specify the lookup table to convert scalars if necessary
  void SetLookupTable(vtkLookupTable *);
  vtkGetObjectMacro(LookupTable,vtkLookupTable);

  // Description:
  // Get Mapped Scalars
  vtkGetObjectMacro(MappedScalars,vtkScalars);

  // Description:
  // Map scalar values into color scalars.
  unsigned char *MapScalarsToColors (vtkScalars *scalars);

protected:
  int   Repeat;
  int   Interpolate;
  int   MapColorScalarsThroughLookupTable;
  vtkStructuredPoints *Input;
  vtkLookupTable *LookupTable;
  vtkScalars *MappedScalars;
  
  // this is to duplicated the previous behavior of SelfCreatedLookUpTable
  int SelfAdjustingTableRange;
};

#endif
