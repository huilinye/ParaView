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
// .NAME vtkVolumeMapper - Abstract class for a volume mapper

// .SECTION Description
// vtkVolumeMapper is the abstract definition of a volume mapper.  Several
// basic types of volume mappers are supported. There are ray casters, which
// produce an image that must be merged with geometry, there are hardware
// methods that blend with geometry, and some combinations of these.

// .SECTION see also
// vtkVolumeRayCastMapper

#ifndef __vtkVolumeMapper_h
#define __vtkVolumeMapper_h

#include "vtkAbstractMapper.h"
#include "vtkStructuredPoints.h"
#include "vtkImageToStructuredPoints.h"

class vtkRenderer;
class vtkVolume;

#define VTK_RAYCAST_VOLUME_MAPPER        0
#define VTK_FRAMEBUFFER_VOLUME_MAPPER    1
#define VTK_SOFTWAREBUFFER_VOLUME_MAPPER 2

#define VTK_CROP_SUBVOLUME              0x0002000
#define VTK_CROP_FENCE                  0x2ebfeba
#define VTK_CROP_INVERTED_FENCE         0x5140145
#define VTK_CROP_CROSS                  0x0417410
#define VTK_CROP_INVERTED_CROSS         0x7be8bef

class vtkWindow;

class VTK_EXPORT vtkVolumeMapper : public vtkAbstractMapper
{
public:
  vtkVolumeMapper();
  ~vtkVolumeMapper();
  const char *GetClassName() {return "vtkVolumeMapper";};
  void PrintSelf( ostream& os, vtkIndent index );

  // Description:
  // Update the volume rendering pipeline by updating the scalar input
  virtual void Update();

  // Description:
  // Turn On/Off orthogonal clipping. (Clipping planes are
  // perpendicular to the coordinate axes.)
  vtkSetMacro(Clipping,int);
  vtkGetMacro(Clipping,int);
  vtkBooleanMacro(Clipping,int);

  // Description:
  // Get the clipping plane values one at a time
  float GetXminClipPlane() { return this->ClippingPlanes[0]; };
  float GetXmaxClipPlane() { return this->ClippingPlanes[1]; };
  float GetYminClipPlane() { return this->ClippingPlanes[2]; };
  float GetYmaxClipPlane() { return this->ClippingPlanes[3]; };
  float GetZminClipPlane() { return this->ClippingPlanes[4]; };
  float GetZmaxClipPlane() { return this->ClippingPlanes[5]; };

  // Description:
  // Set/Get the ClippingPlanes ( xmin, xmax, ymin, ymax, zmin, zmax )
  void SetClippingPlanes( float a, float b, float c, 
                          float d, float e, float f );
  void SetClippingPlanes( float p[6] ); 
  float *GetClippingPlanes() { return this->ClippingPlanes; };

  // Description:
  // Set the flags for the clipping regions. The clipping planes divide the
  // volume into 27 regions - there is one bit for each region. The regions start
  // from the one containing voxel (0,0,0), moving along the x axis fastest, the
  // y axis next, and the z axis slowest. These are represented from the lowest
  // bit to bit number 27 in the integer containing the flags. There are several
  // convenience functions to set some common configurations - subvolume (the
  // default), fence (between any of the clip plane plairs), inverted fence, 
  // cross (between any two of the clip plane pairs) and inverted cross.
  vtkSetClampMacro( ClippingRegionFlags, int, 0x0, 0x7ffffff );
  vtkGetMacro( ClippingRegionFlags, int );
  void SetClippingRegionFlagsToSubVolume() {this->SetClippingRegionFlags( VTK_CROP_SUBVOLUME );};
  void SetClippingRegionFlagsToFence() {this->SetClippingRegionFlags( VTK_CROP_FENCE );};
  void SetClippingRegionFlagsToInvertedFence() {this->SetClippingRegionFlags( VTK_CROP_INVERTED_FENCE );};
  void SetClippingRegionFlagsToCross() {this->SetClippingRegionFlags( VTK_CROP_CROSS );};
  void SetClippingRegionFlagsToInvertedCross() {this->SetClippingRegionFlags( VTK_CROP_INVERTED_CROSS );};

  // Description:
  // Set/Get the rgb texture input data
  void SetRGBTextureInput( vtkStructuredPoints *rgbTexture );
  void SetRGBTextureInput(vtkImageData *cache)
    {vtkImageToStructuredPoints *tmp = cache->MakeImageToStructuredPoints();
    this->SetRGBTextureInput(tmp->GetOutput()); tmp->Delete();}
  virtual vtkStructuredPoints *GetRGBTextureInput();


  virtual int GetMapperType()=0;

  virtual float *GetRGBAPixelData() {return NULL;};

  // Description:
  // Set/Get the input data
  void SetInput( vtkStructuredPoints * );
  void SetInput(vtkImageData *cache)
    {vtkImageToStructuredPoints *tmp = cache->MakeImageToStructuredPoints();
    this->SetInput(tmp->GetOutput()); tmp->Delete();}
  vtkStructuredPoints *GetInput();


//BTX

  // Description:
  // WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
  // DO NOT USE THIS METHOD OUTSIDE OF THE RENDERING PROCESS
  // Render the volume
  virtual void Render(vtkRenderer *ren, vtkVolume *vol)=0;

  // Description:
  // WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
  // Release any graphics resources that are being consumed by this mapper.
  // The parameter window could be used to determine which graphic
  // resources to release.
  virtual void ReleaseGraphicsResources(vtkWindow *) {};

//ETX


protected:
  int                  Clipping;
  float                ClippingPlanes[6];
  int                  ClippingRegionFlags;
  vtkTimeStamp         BuildTime;
};

inline void vtkVolumeMapper::SetClippingPlanes(float a, float b, float c, 
					       float d, float e, float f )
{
  this->ClippingPlanes[0] = a;
  this->ClippingPlanes[1] = b;
  this->ClippingPlanes[2] = c;
  this->ClippingPlanes[3] = d;
  this->ClippingPlanes[4] = e;
  this->ClippingPlanes[5] = f;
}

inline void vtkVolumeMapper::SetClippingPlanes( float p[6] )
{
  this->ClippingPlanes[0] = p[0];
  this->ClippingPlanes[1] = p[1];
  this->ClippingPlanes[2] = p[2];
  this->ClippingPlanes[3] = p[3];
  this->ClippingPlanes[4] = p[4];
  this->ClippingPlanes[5] = p[5];
}

#endif


