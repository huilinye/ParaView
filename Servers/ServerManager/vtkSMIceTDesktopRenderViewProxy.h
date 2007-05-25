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
// .NAME vtkSMIceTDesktopRenderViewProxy
// .SECTION Description
// IceT Render View that can be used in client server configurations.
//
// This class does not use a MultiViewManager for multi-view configurations,
// instead, that reponsibilty is taken on by the RenderSyncManager.

#ifndef __vtkSMIceTDesktopRenderViewProxy_h
#define __vtkSMIceTDesktopRenderViewProxy_h

#include "vtkSMIceTCompositeViewProxy.h"

class VTK_EXPORT vtkSMIceTDesktopRenderViewProxy : public vtkSMIceTCompositeViewProxy
{
public:
  static vtkSMIceTDesktopRenderViewProxy* New();
  vtkTypeRevisionMacro(vtkSMIceTDesktopRenderViewProxy, vtkSMIceTCompositeViewProxy);
  void PrintSelf(ostream& os, vtkIndent indent);

  // Description:
  // Squirt is a hybrid run length encoding and bit reduction compression
  // algorithm that is used to compress images for transmition from the
  // server to client.  Value of 0 disabled all compression.  Level zero is just
  // run length compression with no bit compression (lossless).
  // Squirt compression is only used for client-server image delivery during
  // InteractiveRender.
  vtkSetClampMacro(SquirtLevel, int, 0, 7);
  vtkGetMacro(SquirtLevel, int);

  // Description:
  // In mutlview configurations, all the render views must share the same
  // instance of the server-side render sync manager. 
  // Use this method to set that shared
  // instance. It must be set before calling CreateVTKObjects() on the view
  // proxy.
  void SetSharedServerRenderSyncManager(vtkSMProxy*);

  // Description:
  // Overridden to pass the GUISize to the RenderSyncManager.
  virtual void SetGUISize(int x, int y);

  // Description:
  // Overridden to pass the ViewPosition to the RenderSyncManager.
  virtual void SetViewPosition(int x, int y);

//BTX
protected:
  vtkSMIceTDesktopRenderViewProxy();
  ~vtkSMIceTDesktopRenderViewProxy();

  // Description:
  // Pre-CreateVTKObjects initialization.
  virtual bool BeginCreateVTKObjects(int numObjects);

  // Description:
  // Post-CreateVTKObjects initialization.
  virtual void EndCreateVTKObjects(int numObjects);

  // Description:
  // Overridden to disable squirt compression.
  virtual void BeginStillRender();

  // Description:
  // Overridden to use user-specified squirt compression.
  virtual void BeginInteractiveRender();

  // Description:
  // Initialize the RenderSyncManager properties. Called in
  // EndCreateVTKObjects().
  virtual void InitializeRenderSyncManager();

  // Description:
  // ImageReductionFactor needs to be set on the RenderSyncManager
  // rather than the ParallelRenderManager.
  virtual void SetImageReductionFactorInternal(int factor);

  // Description:
  // Overridden to pass the UseCompositing state to the RenderSyncManager rather
  // than the ParallelRenderManager.
  virtual void SetUseCompositing(bool usecompositing);

  // Description:
  // Internal method to set the squirt level on the RenderSyncManager.
  void SetSquirtLevelInternal(int level);

  // RenderManager managing client-server rendering.
  vtkSMProxy* RenderSyncManager;
  vtkSMProxy* SharedServerRenderSyncManager;

  int SquirtLevel;

private:
  vtkSMIceTDesktopRenderViewProxy(const vtkSMIceTDesktopRenderViewProxy&); // Not implemented
  void operator=(const vtkSMIceTDesktopRenderViewProxy&); // Not implemented
//ETX
};

#endif

