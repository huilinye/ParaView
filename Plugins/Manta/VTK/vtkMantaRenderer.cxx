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
/*=========================================================================

  Program:   VTK/ParaView Los Alamos National Laboratory Modules (PVLANL)
  Module:    $RCSfile$

Copyright (c) 2007, Los Alamos National Security, LLC

All rights reserved.

Copyright 2007. Los Alamos National Security, LLC. 
This software was produced under U.S. Government contract DE-AC52-06NA25396 
for Los Alamos National Laboratory (LANL), which is operated by 
Los Alamos National Security, LLC for the U.S. Department of Energy. 
The U.S. Government has rights to use, reproduce, and distribute this software. 
NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY,
EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  
If software is modified to produce derivative works, such modified software 
should be clearly marked, so as not to confuse it with the version available 
from LANL.
 
Additionally, redistribution and use in source and binary forms, with or 
without modification, are permitted provided that the following conditions 
are met:
-   Redistributions of source code must retain the above copyright notice, 
    this list of conditions and the following disclaimer. 
-   Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution. 
-   Neither the name of Los Alamos National Security, LLC, Los Alamos National
    Laboratory, LANL, the U.S. Government, nor the names of its contributors
    may be used to endorse or promote products derived from this software 
    without specific prior written permission. 

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR 
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/

#include "vtkManta.h"
#include "vtkMantaRenderer.h"
#include "vtkMantaCamera.h"

#include "vtkActor.h"
#include "vtkCuller.h"
#include "vtkLight.h"
#include "vtkLightCollection.h"
#include "vtkObjectFactory.h"
#include "vtkRendererCollection.h"
#include "vtkRenderWindow.h"
#include "vtkTimerLog.h"

#include <Interface/Light.h>
#include <Interface/LightSet.h>
#include <Interface/Scene.h>
#include <Interface/Object.h>
#include <Interface/Context.h>
#include <Engine/Control/RTRT.h>
#include <Engine/Factory/Create.h>
#include <Engine/Factory/Factory.h>
#include <Engine/Display/NullDisplay.h>
#include <Engine/Display/SyncDisplay.h>
#include <Model/AmbientLights/ConstantAmbient.h>
#include <Model/Lights/HeadLight.h>
#include <Model/Groups/Group.h>
#include <Model/Backgrounds/ConstantBackground.h>
#include <Core/Color/Color.h>
#include <Core/Color/ColorDB.h>
#include <Core/Color/RGBColor.h>

#include <Image/SimpleImage.h>
#include <Engine/Control/RTRT.h>
#include <Engine/Display/SyncDisplay.h>

#include <vtkstd/string>

vtkCxxRevisionMacro(vtkMantaRenderer, "$Revision$");
vtkStandardNewMacro(vtkMantaRenderer);

//----------------------------------------------------------------------------
vtkMantaRenderer::vtkMantaRenderer() :
  EngineInited( false ), EngineStarted( false ), NumberOfWorkers( 8 ),
  IsStereo( false ), MaxDepth( 5 ), MantaScene( 0 ), MantaWorldGroup( 0 ),
  MantaLightSet( 0 ), MantaCamera( 0 ), SyncDisplay( 0 )
{
  cerr << "CREATE MANTA RENDERER " << this << endl;
  // the default global ambient light created by vtkRenderer is too bright.
  this->SetAmbient( 0.1, 0.1, 0.1 );

  this->MantaEngine = Manta::createManta();
  this->MantaEngine->changeNumWorkers( this->NumberOfWorkers );

  // Default options
  this->MantaFactory = new Manta::Factory( this->MantaEngine );
  //this->MantaFactory->selectImageType( "rgbafloat" );
  //this->MantaFactory->selectImageType( "rgbzfloat" );
  this->MantaFactory->selectImageType( "rgba8zfloat" );
  this->MantaFactory->selectImageTraverser( "tiled(-square)" );
  //this->MantaFactory->selectImageTraverser( "deadline()" );
  this->MantaFactory->selectLoadBalancer( "workqueue" );
  //this->MantaFactory->selectShadowAlgorithm( "hard(-attenuateShadows)" );
  this->MantaFactory->selectShadowAlgorithm( "noshadows" );
  this->MantaFactory->selectPixelSampler( "singlesample" );
  //this->MantaFactory->selectPixelSampler("regularsample(-numberOfSamples 4)");
  //this->MantaFactory->selectPixelSampler(
  //"jittersample(-numberOfSamples 16)");
  this->MantaFactory->selectRenderer( "raytracer" );
  //this->MantaFactory->selectRenderer( "depthvalue" );

  this->ColorBuffer = NULL;
  this->DepthBuffer = NULL;
  this->ImageSize = -1;
}

//----------------------------------------------------------------------------
vtkMantaRenderer::~vtkMantaRenderer()
{
  cerr << "DESTROY MANTA RENDERER " << this << endl;

  // don't do anything if the engine is not even initialized
  // it is the case for the 2nd renderer in PV.
  if ( !this->EngineInited )
    {
    delete this->MantaFactory;
    delete this->MantaEngine;
    return;
    }

  if ( this->EngineStarted )
    {
    // Stop the Manta Engine
    this->GetMantaEngine()->finish();
    this->GetSyncDisplay()->doneRendering();
    this->GetMantaEngine()->blockUntilFinished();
    }

  // we don't have to delete each Manta::Light in the Manta::LightSet, they
  // are deleted by ~vtkMantaLight()
  delete this->MantaLightSet->getAmbientLight();
  delete this->MantaLightSet;
  delete this->MantaCamera;

  // Manta::Scene is not responsible for de-allocating the referenced
  // (shallow-copied) Manta::ConstantBackground object created in Initialize()
  delete this->MantaScene->getBackground();
  this->MantaScene->setBackground( NULL );
  delete this->MantaScene;

  delete this->MantaWorldGroup;
  delete this->MantaFactory;
  delete this->MantaEngine;
#if 0
  delete this->SyncDisplay;
#endif

  if (this->ColorBuffer)
    {
    delete[] this->ColorBuffer;
    }
  if (this->DepthBuffer)
    {
    delete[] this->DepthBuffer;
    }
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::InitEngine()
{
  // create an empty Manta scene with background
  this->MantaScene = new Manta::Scene();
  this->MantaScene->getRenderParameters().setMaxDepth( this->MaxDepth );
  double *color = this->GetBackground();
  Manta::ConstantBackground * background = new Manta::ConstantBackground(
    Manta::Color(  Manta::RGBColor( color[0], color[1], color[2] )  )  );
  this->MantaScene->setBackground( background );

  // create empty world group
  this->MantaWorldGroup = new Manta::Group();
  this->MantaScene->setObject( this->MantaWorldGroup );

  // create empty LightSet with ambient light
  double *ambient = this->Ambient;
  this->MantaLightSet = new Manta::LightSet();
  this->MantaLightSet->setAmbientLight( new Manta::ConstantAmbient(
    Manta::Color(  Manta::RGBColor( ambient[0], ambient[1], ambient[2] )
                ) ) );
  this->MantaScene->setLights( this->MantaLightSet );
  this->MantaEngine->setScene( this->MantaScene );

  // create the mantaCamera singleton,
  // it is the only camera we create per renderer
  this->MantaCamera = this->MantaFactory->
    createCamera( "pinhole(-normalizeRays -createCornerRays)" );

  // Use SyncDisplay with Null Display to stop Manta engine at each frame,
  // the image is combined with OpenGL framebuffer by vtkXMantaRenderWindow
  vtkstd::vector<vtkstd::string> vs;
  this->SyncDisplay = new Manta::SyncDisplay( vs );
  // TODO: memory leak, NullDisplay is not deleted
  this->SyncDisplay->setChild(  new Manta::NullDisplay( vs )  );

  //Set screen size
  int *size = this->GetSize();
  this->ChannelId = this->MantaEngine->createChannel( this->SyncDisplay,
    this->MantaCamera, this->IsStereo, size[0], size[1] );

  this->EngineInited = true;
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::SetBackground(double r, double g, double b)
{
  if ((this->Background[0] != r)||
      (this->Background[1] != g)||
      (this->Background[2] != b))
  {
  this->Superclass::SetBackground(r,g,b);
  this->MantaEngine->addTransaction
    ( "set background",
      Manta::Callback::create( this, &vtkMantaRenderer::SetMantaBackground) );
  }; 
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::SetMantaBackground()
{
  cerr << "CALLBACK" << endl;
  double *color = this->GetBackground();
  Manta::ConstantBackground * background = new Manta::ConstantBackground(
    Manta::Color(  Manta::RGBColor( color[0], color[1], color[2] )  )  );
  this->MantaScene->setBackground( background );
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::ChangeNumberOfWorkers(int numWorkers)
{
  if (this->NumberOfWorkers == numWorkers)
    {
    return;
    }
  this->NumberOfWorkers = numWorkers;
  this->MantaEngine->changeNumWorkers( this->NumberOfWorkers );
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::ClearLights(void)
{
  //TODO: This change needs to be scheduled
  delete this->MantaLightSet->getAmbientLight();

  for ( unsigned int i = 0; i < this->MantaLightSet->numLights(); i ++ )
    {
    Manta::Light *currentLight = this->MantaLightSet->getLight( i );
    this->MantaLightSet->remove( currentLight );
    delete currentLight;
    }
}

//----------------------------------------------------------------------------
// Ask lights to load themselves into graphics pipeline.
int vtkMantaRenderer::UpdateLights()
{
  // convert VTK lights into Manta lights
  vtkCollectionSimpleIterator sit;
  this->Lights->InitTraversal( sit );

//    Manta::Light *headlight =
//      new Manta::HeadLight(    0, Manta::Color(  Manta::RGB( .5, .5, .5 )  )   );
//    this->MantaLightSet->add(headlight);

  if ( this->Lights->GetNextLight( sit ) == 0 &&
       this->MantaLightSet->numLights()  == 0 )
    {
    // there is no VTK light nor MantaLight defined, create a Manta headlight
    // TODO: memory leak, headlight is not deleted
    vtkWarningMacro(
      << "No light defined, creating a headlight at camera position" );
    Manta::Light *headlight =
      new Manta::HeadLight(    0, Manta::Color(  Manta::RGBColor( 1, 1, 1 )  )   );

    this->MantaEngine->addTransaction( "add headlight",
      Manta::Callback::create( this->MantaLightSet, &Manta::LightSet::add,
      headlight ) );
    }
  else
    {
    // TODO: schedule ClearLight here?
    // TODO: the LightKit in ParaView with MantaView creates vtkOpenGLight rather
    // than vtkMantaLight because there is no Client/Server communication involved
    vtkLight *vLight = NULL;
    for ( this->Lights->InitTraversal( sit );
          ( vLight = this->Lights->GetNextLight( sit ) ) ; )
      {
      if ( vLight->GetSwitch() )
        {
        vLight->Render( this, 0 /* not used */ );
        }
      }
    }
  return 0;
}

//----------------------------------------------------------------------------
vtkCamera* vtkMantaRenderer::MakeCamera()
{
  return vtkMantaCamera::New();
}

//----------------------------------------------------------------------------
void vtkMantaRenderer::DeviceRender()
{
  // In ParaView, we are wasting time in rendering the "sync layer" with
  // empty background image just to be dropped in LayerRender(). We just
  // don't start the engine with sync layer.
  // TODO: this may not be the right way to check if it is a sync layer
  if (this->GetLayer() != 0 && this->GetActors()->GetNumberOfItems() == 0)
    {
    return;
    }

  // Initialize the Manta engine so it can accept geometry
  // but don't start rendering just yet.
  if ( !this->EngineInited )
    {
    this->InitEngine();
    }

  vtkTimerLog::MarkStartEvent("Geometry");

  // call camera::Render()
  this->UpdateCamera();

  // TODO: call ClearLights here?

  // call Light::Render()
  this->UpdateLightGeometry();
  this->UpdateLights();

  this->UpdateGeometry();

  vtkTimerLog::MarkEndEvent("Geometry");

  // Start the Manta Engine so the geometry added
  // by transactions can be rendered
  if (!this->EngineStarted)
    {
    this->MantaEngine->beginRendering( false );
    this->EngineStarted = true;
    }

  vtkTimerLog::MarkStartEvent("Total LayerRender");
  if ( this->EngineStarted )
    {
    this->LayerRender();
    }
  vtkTimerLog::MarkEndEvent("Total LayerRender");
}

//----------------------------------------------------------------------------
// let the renderer display itself appropriately based on its layer index
void vtkMantaRenderer::LayerRender()
{
  int     i, j;
  int     rowLength,  mantaSize[2];
  int     minWidth,   minHeight;
  int     hMantaDiff, hRenderDiff;
  int     renderPos0[2];
  int*    renderSize  = NULL;
  int*    renWinSize  = NULL;
  bool    stereoDumy;
  float*  mantaBuffer = NULL;
  double* renViewport = NULL;
  const   Manta::SimpleImageBase* mantaBase = NULL;


  vtkTimerLog::MarkStartEvent("ThreadSync");
  // syncrhonize with render threads to be sure manta has a full set of pixels
  this->GetSyncDisplay()->waitOnFrameReady();
  vtkTimerLog::MarkEndEvent("ThreadSync");

  if (this->GetLayer() != 0 && this->NumberOfPropsRendered == 0)
    {
    // skip image composition if we are not Layer 0 and nothing is rendered
    // in this layer.
    //cerr << "empty layer: " << this->GetLayer() << endl;
    this->GetSyncDisplay()->doneRendering();
    return;
    }

  // collect some useful info
  renderSize = this->GetSize();
  renWinSize = this->GetRenderWindow()->GetActualSize();
  renViewport= this->GetViewport();
  renderPos0[0] = int( renViewport[0] * renWinSize[0] + 0.5f );
  renderPos0[1] = int( renViewport[1] * renWinSize[1] + 0.5f );
  this->GetSyncDisplay()->getCurrentImage()->
        getResolution( stereoDumy, mantaSize[0], mantaSize[1] );
  mantaBase = dynamic_cast< const Manta::SimpleImageBase * >
              ( this->GetSyncDisplay()->getCurrentImage() );
  rowLength = mantaBase->getRowLength();

//  cerr << endl << "vtkMantaRenderer, ImangeSize: "
//       << mantaSize[0] << ", " << mantaSize[1] << ", "
//       << "renWinSize: " << renWinSize[0] << ", " << renWinSize[1] << ", "
//       << "renderSize: " << renderSize[0] << ", " << renderSize[1] << ", " << endl
//       << "Layer: " << this->GetLayer() << ", Props Rendered: " << this->NumberOfPropsRendered << endl;


  // for window re-sizing
  minWidth    = ( mantaSize[0] < renderSize[0] )
                ? mantaSize[0] : renderSize[0];
  minHeight   = ( mantaSize[1] < renderSize[1] )
                ? mantaSize[1] : renderSize[1];
  hMantaDiff  = mantaSize[1] - minHeight;
  hRenderDiff = renderSize[1]- minHeight;
  if (hMantaDiff != 0 || hRenderDiff != 0)
     {
     cerr << "mantaDiff: " << hMantaDiff << endl;
     cerr << "RenderDiff: " << hRenderDiff << endl;
     }

  // memory allocation and acess to the Manta image
  int size = renderSize[0]*renderSize[1];
  if (this->ImageSize != size)
    {
    delete[] this->ColorBuffer;
    delete[] this->DepthBuffer;
    this->ImageSize = size;
    this->DepthBuffer = new float[ size ];
    this->ColorBuffer = new float[ size ];
    }
  mantaBuffer = static_cast< float * >( mantaBase->getRawData(0) );

  // update this->ColorBuffer and this->DepthBuffer from the Manta
  // RGBA8ZfloatPixel array
  double *clipValues = this->GetActiveCamera()->GetClippingRange();
  double depthScale  = 1.0f / ( clipValues[1] - clipValues[0] );

  vtkTimerLog::MarkStartEvent("Image Conversion");
  // This double for loop costs about 0.01 seconds per frames on the
  // 8 cores machine. This can be fixed with RGBA8ZFloatP
  for ( j = 0; j < minHeight; j ++ )
    {
    // there are two floats in each pixel in Manta buffer
    int mantaIndex = ( ( j + hMantaDiff  ) * rowLength     ) * 2;
    // there is only one float in each pixel in the GL RGBA or Z buffer
    int tupleIndex = ( ( j + hRenderDiff ) * renderSize[0] ) * 1;

    for ( i = 0; i < minWidth; i ++ )
      {
      this->ColorBuffer[ tupleIndex + i     ]
                         = mantaBuffer[ mantaIndex + i*2     ];
      float depthValue   = mantaBuffer[ mantaIndex + i*2 + 1 ];
      // normalize the depth values to [ 0.0f, 1.0f ], since we are using a
      // software buffer for Z values and never write them to OpenGL buffers,
      // we don't have to clamp them any more
      // TODO: On a second thought, we probably don't even have to normalize Z
      // values at all
      this->DepthBuffer[ tupleIndex + i ]
                         = ( depthValue - clipValues[0] ) * depthScale;
      }
    }

  // decouple to let render threads work right away
  this->GetSyncDisplay()->doneRendering();
  vtkTimerLog::MarkEndEvent("Image Conversion");

}

//----------------------------------------------------------------------------
void vtkMantaRenderer::PrintSelf( ostream& os, vtkIndent indent )
{
  this->Superclass::PrintSelf( os, indent );
}
