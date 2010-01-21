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
#include "vtkAMRDualGridHelper.h"
#include "vtkObjectFactory.h"
#include "vtkMultiProcessController.h"
#include "vtkImageData.h"
#include "vtkUniformGrid.h"
#include "vtkHierarchicalBoxDataSet.h"
#include "vtkAMRBox.h"
#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkUnsignedCharArray.h"
#include "vtkstd/vector"

vtkCxxRevisionMacro(vtkAMRDualGridHelper, "$Revision$");
vtkStandardNewMacro(vtkAMRDualGridHelper);

class vtkAMRDualGridHelperSeed;

// For debugging only
//vtkPolyData* DebuggingPolyData;
//vtkIntArray* DebuggingAttributes;
//static double DebuggingGlobalOrigin[3];
//static double DebuggingRootSpacing[3];



//============================================================================
// Helper object for getting information from AMR datasets.
// API: 
// Have a block object as part of the API? Yes; Level? No.
// Initialize helper with a CTH dataset.
// Get GlobalOrigin, RootSpacing, NumberOfLevels
//     ?StandardCellDimensions(block with ghost levels)
// Get NumberOfBlocksInLevel (level);
// GetBlock(level, blockIdx)
// BlockAPI.


// Neighbors: Specify a block with level and grid position.
//     Get NumberOfNeighbors on any of the six faces.

//----------------------------------------------------------------------------
class vtkAMRDualGridHelperLevel
{
public:
  vtkAMRDualGridHelperLevel();
  ~vtkAMRDualGridHelperLevel();

  // Level is stored implicitely in the Helper,
  // but it can't hurn to have it here too.
  int Level;

  void CreateBlockFaces(vtkAMRDualGridHelperBlock* block, int x, int y, int z);
  vtkstd::vector<vtkAMRDualGridHelperBlock*> Blocks;

  // I need my own container because the 2D 
  // grid can expand in all directions.
  // the block in grid index 0,0 has its origin on the global origin.
  // I think I will make this grid temporary for initialization only.
  int GridExtent[6];
  int GridIncY;
  int GridIncZ;
  vtkAMRDualGridHelperBlock** Grid;

  vtkAMRDualGridHelperBlock* AddGridBlock(int x, int y, int z, vtkImageData* volume);
  vtkAMRDualGridHelperBlock* GetGridBlock(int x, int y, int z);

private:
};

//----------------------------------------------------------------------------
// Degenerate regions that span processes are kept them in a queue
// to communicate and process all at once.  This is the queue item.
class vtkAMRDualGridHelperDegenerateRegion
{
public:
  vtkAMRDualGridHelperDegenerateRegion();
  vtkAMRDualGridHelperBlock* ReceivingBlock;
  int ReceivingRegion[3];
  vtkAMRDualGridHelperBlock* SourceBlock;
};
vtkAMRDualGridHelperDegenerateRegion::vtkAMRDualGridHelperDegenerateRegion()
{
  this->ReceivingBlock = this->SourceBlock = 0;
  this->ReceivingRegion[0] = 0;
  this->ReceivingRegion[1] = 0;
  this->ReceivingRegion[2] = 0;
}

//----------------------------------------------------------------------------
vtkAMRDualGridHelperSeed::vtkAMRDualGridHelperSeed()
{
  this->Index[0] = -1;
  this->Index[1] = -1;
  this->Index[2] = -1;
  this->FragmentId = 0;
}
//****************************************************************************
vtkAMRDualGridHelperLevel::vtkAMRDualGridHelperLevel()
{
  this->Level = 0;
  this->Grid = 0;
  for (int ii = 0; ii < 3; ++ii)
    {
    this->GridExtent[2*ii] = 0;
    this->GridExtent[2*ii + 1] = -1;
    }
}
//----------------------------------------------------------------------------
vtkAMRDualGridHelperLevel::~vtkAMRDualGridHelperLevel()
{
  int ii;
  int num = (int)(this->Blocks.size());

  this->Level = -1;
  for (ii = 0; ii < num; ++ii)
    {
    if (this->Blocks[ii])
      {
      delete this->Blocks[ii];
      this->Blocks[ii] = 0;
      }
    }

  for (ii = 0; ii < 6; ++ii)
    {
    this->GridExtent[ii] = 0;
    }
  // The grid does not "own" the blocks
  // so it does not need to delete them.
  if (this->Grid)
    {
    delete [] this->Grid;
    this->Grid = 0;
    }
}
//----------------------------------------------------------------------------
vtkAMRDualGridHelperBlock* vtkAMRDualGridHelperLevel::GetGridBlock(
  int x,
  int y,
  int z)
{
  if (x < this->GridExtent[0] || x > this->GridExtent[1])
    {
    return 0;
    }
  if (y < this->GridExtent[2] || y > this->GridExtent[3])
    {
    return 0;
    }
  if (z < this->GridExtent[4] || z > this->GridExtent[5])
    {
    return 0;
    }

  return this->Grid[x+ y*this->GridIncY + z*this->GridIncZ];
}

//----------------------------------------------------------------------------
// This method is meant to be called after all the blocks are created and 
// in their level grids.  It shoud also be called after FindExistingFaces
// is called for this level, but before FindExisitngFaces is called for
// higher levels.
void vtkAMRDualGridHelperLevel::CreateBlockFaces(
  vtkAMRDualGridHelperBlock *block,
  int x,
  int y,
  int z)
{
  // avoid a warning.
  int temp = x+y+z+block->Level;
  if (temp < 1)
    {
    return;
    }
  
  /*
  vtkAMRDualGridHelperBlock* neighborBlock;
  if (block == 0)
    {
    return;
    }

  // The faces are for connectivity seeds between blocks.
  vtkAMRDualGridHelperFace* face;
  // -x Check for an exiting face in this level
  neighborBlock = this->GetGridBlock(x-1,y,z);
  if (neighborBlock && neighborBlock->Faces[1])
    {
    block->SetFace(0, neighborBlock->Faces[1]);
    }
  if (block->Faces[0] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 0);
    block->SetFace(0, face);
    }

  // +x Check to for an exiting face in this level
  neighborBlock = this->GetGridBlock(x+1,y,z);
  if (neighborBlock && neighborBlock->Faces[0])
    {
    block->SetFace(1, neighborBlock->Faces[0]);
    }
  if (block->Faces[1] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 1);
    block->SetFace(1, face);
    }

  // -y Check to for an exiting face in this level
  neighborBlock = this->GetGridBlock(x,y-1,z);
  if (neighborBlock && neighborBlock->Faces[3])
    {
    block->SetFace(2, neighborBlock->Faces[3]);
    }
  if (block->Faces[2] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 2);
    block->SetFace(2, face);
    }

  // +y Check to for an exiting face in this level
  neighborBlock = this->GetGridBlock(x,y+1,z);
  if (neighborBlock && neighborBlock->Faces[2])
    {
    block->SetFace(3, neighborBlock->Faces[2]);
    }
  if (block->Faces[3] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 3);
    block->SetFace(3, face);
    }

  // -z Check to for an exiting face in this level
  neighborBlock = this->GetGridBlock(x,y,z-1);
  if (neighborBlock && neighborBlock->Faces[5])
    {
    block->SetFace(4, neighborBlock->Faces[5]);
    }
  if (block->Faces[4] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 4);
    block->SetFace(4, face);
    }

  // +z Check to for an exiting face in this level
  neighborBlock = this->GetGridBlock(x,y,z+1);
  if (neighborBlock && neighborBlock->Faces[4])
    {
    block->SetFace(5, neighborBlock->Faces[4]);
    }
  if (block->Faces[5] == 0)
    { // create a new face.
    face = new vtkAMRDualGridHelperFace;
    face->InheritBlockValues(block, 5);
    block->SetFace(5, face);
    }
    */
}
//****************************************************************************
vtkAMRDualGridHelperBlock::vtkAMRDualGridHelperBlock()
{
  this->UserData = 0;

  //int ii;
  this->Level = 0;
  this->OriginIndex[0] = 0;
  this->OriginIndex[1] = 0;
  this->OriginIndex[2] = 0;

  this->GridIndex[0] = 0;
  this->GridIndex[1] = 0;
  this->GridIndex[2] = 0;

  this->ProcessId = vtkMultiProcessController::GetGlobalController()->GetLocalProcessId();

  //for (ii = 0; ii < 6; ++ii)
  //  {
  //  this->Faces[ii] = 0;
  //  }
  this->Image = 0;
  this->CopyFlag = 0;

  for (int x = 0; x < 3; ++x)
    {
    for (int y = 0; y < 3; ++y)
      {
      for (int z = 0; z < 3; ++z)
        {
        // Default to own.
        this->RegionBits[x][y][z] = vtkAMRRegionBitOwner;
        }
      }
    }
  // It does not matter what the center is because we do not reference it.
  // I cannot hurt to set it consistently though.
  this->RegionBits[1][1][1] = vtkAMRRegionBitOwner;

  // Default to boundary.
  this->BoundaryBits = 63;
}
//----------------------------------------------------------------------------
vtkAMRDualGridHelperBlock::~vtkAMRDualGridHelperBlock()
{
  if (this->UserData)
    {
    // It is not a vtkObject yet.
    //this->UserData->Delete();
    this->UserData = 0;
    }

  int ii;
  this->Level = 0;
  this->OriginIndex[0] = 0;
  this->OriginIndex[1] = 0;
  this->OriginIndex[2] = 0;

  // I broke down and made faces reference counted.
  for (ii = 0; ii < 6; ++ii)
    {
    //if (this->Faces[ii])
    //  {
    //  this->Faces[ii]->Unregister();
    //  this->Faces[ii] = 0;
    //  }
    }
  if (this->Image)
    {
    if (this->CopyFlag)
      { // We made a copy of the image and have to delete it.
      this->Image->Delete();
      }
    this->Image = 0;
    }
}
//----------------------------------------------------------------------------
template <class T>
void vtkAMRDualGridHelperAddBackGhostValues(T *inPtr, int inDim[3],
                                           T *outPtr, int outDim[3],
                                           int offset[3])
{
  T *inPtrX, *inPtrY, *inPtrZ;
  int xx, yy, zz;
  int inIncZ = inDim[0] * inDim[1];
  int inExt[6];
  int outExt[6];
  
  // out always has ghost.
  outExt[0] = outExt[2] = outExt[4] = -1;
  outExt[1] = outExt[0] + outDim[0] - 1;
  outExt[3] = outExt[2] + outDim[1] - 1;
  outExt[5] = outExt[4] + outDim[2] - 1;
  inExt[0] = -1 + offset[0];
  inExt[2] = -1 + offset[1];
  inExt[4] = -1 + offset[2];
  inExt[1] = inExt[0] + inDim[0] - 1;
  inExt[3] = inExt[2] + inDim[1] - 1;
  inExt[5] = inExt[4] + inDim[2] - 1;
  
  
  inPtrZ = inPtr;
  for (zz = outExt[4]; zz <= outExt[5]; ++zz)
    {
    inPtrY = inPtrZ;
    for (yy = outExt[2]; yy <= outExt[3]; ++yy)
      {
      inPtrX = inPtrY;
      for (xx = outExt[0]; xx <= outExt[1]; ++xx)
        {
        *outPtr++ = *inPtrX;
        if (xx >= inExt[0] && xx < inExt[1])
          {
          ++inPtrX;
          }
        }
      if (yy >= inExt[2] && yy < inExt[3])
        {
        inPtrY += inDim[0];
        }
      }
    if (zz >= inExt[4] && zz < inExt[5])
      {
      inPtrZ += inIncZ;
      }
    }
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelperBlock::AddBackGhostLevels(int standardBlockDimensions[3])
{
  int ii;
  int inDim[3];
  int outDim[3];
  if (this->Image == 0)
    {
    vtkGenericWarningMacro("Missing image.");
    return;
    }
  this->Image->GetDimensions(inDim);
  this->Image->GetDimensions(outDim);
  double origin[3];
  this->Image->GetOrigin(origin);
  double *spacing = this->Image->GetSpacing(); 
  
  // Note.  I as assume that origin index is the index of the first pixel
  // not the index of 0.

  int needToCopy = 0;
  int offset[3];
  int nCheck[3];
  int pCheck[3];
  for (ii = 0; ii < 3; ++ii)
    {
    // Conversion from point dims to cell dims.
    --inDim[ii];
    --outDim[ii];

    // Check negative axis.  
    nCheck[ii] = this->OriginIndex[ii] % standardBlockDimensions[ii];
    // Check positive axis
    pCheck[ii] = (this->OriginIndex[ii]+inDim[ii]) % standardBlockDimensions[ii];
    offset[ii] = 0;
    if (nCheck[ii] == 0)
      {
      this->OriginIndex[ii] = this->OriginIndex[ii] - 1;
      origin[ii] = origin[ii] - spacing[ii];
      offset[ii] = 1;
      ++outDim[ii];
      needToCopy = 1;
      }
    if (pCheck[ii] == 0)
      {
      ++outDim[ii];
      needToCopy = 1;
      }
    }
    
  if ( ! needToCopy)
    {
    return;
    }

  vtkIdType newSize = (outDim[0]*outDim[1]*outDim[2]);

  vtkImageData* copy = vtkImageData::New();
  copy->SetDimensions(outDim[0]+1, outDim[1]+1, outDim[2]+1);
  copy->SetSpacing(spacing);
  copy->SetOrigin(origin);
  // Copy only cell arrays.
  int numArrays = this->Image->GetCellData()->GetNumberOfArrays();
  for (int idx = 0; idx < numArrays; ++idx)
    {
    vtkDataArray* da = this->Image->GetCellData()->GetArray(idx);
    vtkAbstractArray* copyArray = da->CreateArray(da->GetDataType());
    copyArray->SetNumberOfComponents(da->GetNumberOfComponents());
    copyArray->SetNumberOfTuples(newSize);
    copyArray->SetName(da->GetName());
    switch (da->GetDataType())
      {
      vtkTemplateMacro(vtkAMRDualGridHelperAddBackGhostValues( 
           static_cast<VTK_TT *>(da->GetVoidPointer(0)), inDim,
           static_cast<VTK_TT *>(copyArray->GetVoidPointer(0)), outDim,
           offset));
      default:
        vtkGenericWarningMacro("Execute: Unknown output ScalarType");
        return;
      }
    copy->GetCellData()->AddArray(copyArray);
    copyArray->Delete();
    }

  this->Image = copy;
  this->CopyFlag = 1;
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelperBlock::SetFace(
  int faceId,
  vtkAMRDualGridHelperFace* face)
{
  // Just in case.
  vtkAMRDualGridHelperFace* tmp = this->Faces[faceId];
  if (tmp)
    {
    --(tmp->UseCount);
    if (tmp->UseCount <= 0)
      {
      delete tmp;
      }
    this->Faces[faceId] = 0;
    }

  if (face)
    {
    ++(face->UseCount);
    this->Faces[faceId] = face;
    }
}

//****************************************************************************
vtkAMRDualGridHelperFace::vtkAMRDualGridHelperFace()
{
  this->Level = 0;
  this->NormalAxis = 0;
  this->OriginIndex[0] = 0;
  this->OriginIndex[1] = 0;
  this->OriginIndex[2] = 0;
  this->UseCount = 0;
}
//----------------------------------------------------------------------------
vtkAMRDualGridHelperFace::~vtkAMRDualGridHelperFace()
{
  this->Level = 0;
  this->NormalAxis = 0;
  this->OriginIndex[0] = 0;
  this->OriginIndex[1] = 0;
  this->OriginIndex[2] = 0;
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelperFace::InheritBlockValues(vtkAMRDualGridHelperBlock* block, int faceIndex)
{
  // avoid warning.
  faceIndex = block->Level;
  /* we are not worring about connectivity yet.
  int* ext = block->Image->GetExtent();
  this->Level = block->Level;
  this->OriginIndex[0] = block->OriginIndex[0];
  this->OriginIndex[1] = block->OriginIndex[1];
  this->OriginIndex[2] = block->OriginIndex[2];
  switch (faceIndex)
    {
    case 0:
      this->NormalAxis = 0;
      break;
    case 1:
      this->NormalAxis = 0;
      this->OriginIndex[0] += ext[1]-ext[0];
      break;
    case 2:
      this->NormalAxis = 1;
      break;
    case 3:
      this->NormalAxis = 1;
      ++this->OriginIndex[1] += ext[3]-ext[2];
      break;
    case 4:
      this->NormalAxis = 2;
      break;
    case 5:
      this->NormalAxis = 2;
      ++this->OriginIndex[2] += ext[5]-ext[4];
      break;
    }
  */
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelperFace::Unregister()
{
  --this->UseCount;
  if (this->UseCount <= 0)
    {
    delete this;
    }
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelperFace::AddFragmentSeed(int level, int x, int y, int z, 
                                              int fragmentId)
{
  //double pt[3];
  // This is a dual point so we need to shift it to the middle of a cell.
//  pt[0] = DebuggingGlobalOrigin[0] + ((double)(x)+0.5) * DebuggingRootSpacing[0] / (double)(1<<level);
//  pt[1] = DebuggingGlobalOrigin[1] + ((double)(y)+0.5) * DebuggingRootSpacing[1] / (double)(1<<level);
//  pt[2] = DebuggingGlobalOrigin[2] + ((double)(z)+0.5) * DebuggingRootSpacing[2] / (double)(1<<level);
//  vtkIdType ptIds[1];
//  ptIds[0] = DebuggingPolyData->GetPoints()->InsertNextPoint(pt);
//  DebuggingPolyData->GetVerts()->InsertNextCell(1, ptIds);
//  DebuggingAttributes->InsertNextTuple1(ptIds[0]);

  // I expect that we will never add seeds from a different level.
  // Faces are always the lower level of the two blocks.
  // We process lower level blocks first.
  if (level != this->Level)
    {
    vtkGenericWarningMacro("Unexpected level.");
    return;
    }
  vtkAMRDualGridHelperSeed seed;
  seed.Index[0] = x;
  seed.Index[1] = y;
  seed.Index[2] = z;
  seed.FragmentId = fragmentId;

  this->FragmentIds.push_back(seed);
}
//****************************************************************************
vtkAMRDualGridHelper::vtkAMRDualGridHelper()
{
  int ii;
  
  this->SkipGhostCopy = 0;

  this->DataTypeSize = 8;
  this->ArrayName = 0;
  this->EnableDegenerateCells = 1;
  this->NumberOfBlocksInThisProcess = 0;
  for (ii = 0; ii < 3; ++ii)
    {
    this->StandardBlockDimensions[ii] = 0;
    this->RootSpacing[ii] = 1.0;
    this->GlobalOrigin[ii] = 0.0;
    }
  this->Controller = vtkMultiProcessController::GetGlobalController();

  this->MessageBuffer  = 0;
  this->MessageBufferLength  = 0;
}
//----------------------------------------------------------------------------
vtkAMRDualGridHelper::~vtkAMRDualGridHelper()
{
  int ii;
  int numberOfLevels = (int)(this->Levels.size());

  this->SetArrayName(0);

  for (ii = 0; ii < numberOfLevels; ++ii)
    {
    delete this->Levels[ii];
    this->Levels[ii] = 0;
    }

  // Todo: See if we really need this.
  this->NumberOfBlocksInThisProcess = 0;

  if (this->MessageBuffer)
    {
    delete [] this->MessageBuffer;
    this->MessageBuffer  = 0;
    this->MessageBufferLength  = 0;
    }
    
  this->DegenerateRegionQueue.clear();
}
//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::SetEnableMultiProcessCommunication(int v)
{
  if (v)
    {
    this->Controller = vtkMultiProcessController::GetGlobalController();
    }
  else
    {
    this->Controller = 0;
    }
}

//----------------------------------------------------------------------------
int vtkAMRDualGridHelper::GetNumberOfBlocksInLevel(int level)
{
  if (level < 0 || level >= (int)(this->Levels.size()))
    {
    return 0;
    }
  return (int)(this->Levels[level]->Blocks.size());
}

//----------------------------------------------------------------------------
vtkAMRDualGridHelperBlock* vtkAMRDualGridHelper::GetBlock(int level, int blockIdx)
{
  if (level < 0 || level >= (int)(this->Levels.size()))
    {
    return 0;
    }
  if ((int)(this->Levels[level]->Blocks.size()) <= blockIdx)
    {
    return 0;
    }
  return this->Levels[level]->Blocks[blockIdx];
}

//----------------------------------------------------------------------------
vtkAMRDualGridHelperBlock* vtkAMRDualGridHelper::GetBlock(
  int level, int xGrid, int yGrid, int zGrid)
{
  if (level < 0 || level >= (int)(this->Levels.size()))
    {
    return 0;
    }
  return this->Levels[level]->GetGridBlock(xGrid, yGrid, zGrid);
}

//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::AddBlock(int level, vtkImageData* volume)
{
  // For sending degenerate array values we need to know the type.
  // This assumes all images are the same type (of course).
  vtkDataArray* da = volume->GetCellData()->GetArray(this->ArrayName);
  if (da)
    {
    this->DataTypeSize = da->GetDataTypeSize();
    }
  else
    {
    vtkErrorMacro("Could not find the data type size.");
    }

  // First compute the grid location of this block.
  double blockSize[3];
  blockSize[0] = (this->RootSpacing[0]*this->StandardBlockDimensions[0]) / (1 << level);
  blockSize[1] = (this->RootSpacing[1]*this->StandardBlockDimensions[1]) / (1 << level);
  blockSize[2] = (this->RootSpacing[2]*this->StandardBlockDimensions[2]) / (1 << level);
  double *bounds = volume->GetBounds();
  double center[3];
  
  center[0] = (bounds[0]+bounds[1]) * 0.5;
  center[1] = (bounds[2]+bounds[3]) * 0.5;
  center[2] = (bounds[4]+bounds[5]) * 0.5;
  int x = (int)((center[0]-this->GlobalOrigin[0])/blockSize[0]);
  int y = (int)((center[1]-this->GlobalOrigin[1])/blockSize[1]);
  int z = (int)((center[2]-this->GlobalOrigin[2])/blockSize[2]);
  vtkAMRDualGridHelperBlock* block = 
    this->Levels[level]->AddGridBlock(x, y, z, volume);

  // We need to set this ivar here because we need to compute the index 
  // from the global origin and root spacing.  The issue is that some blocks
  // may not ghost levels.  Everything would be easier if the
  // vtk spy reader did not strip off ghost cells of the outer blocks.
  int* ext = volume->GetExtent();
  double* spacing = volume->GetSpacing();
  double origin[3];
  volume->GetOrigin(origin);
  // Move the origin to the first voxel.
  origin[0] += spacing[0]*(double)(ext[0]);
  origin[1] += spacing[1]*(double)(ext[2]);
  origin[2] += spacing[2]*(double)(ext[4]);
  // Now convert the origin into a level index.
  origin[0] -= this->GlobalOrigin[0];
  origin[1] -= this->GlobalOrigin[1];
  origin[2] -= this->GlobalOrigin[2];
  block->OriginIndex[0] = (int)(0.5 + origin[0] * (double)(1<<level) / (this->RootSpacing[0]));
  block->OriginIndex[1] = (int)(0.5 + origin[1] * (double)(1<<level) / (this->RootSpacing[1]));
  block->OriginIndex[2] = (int)(0.5 + origin[2] * (double)(1<<level) / (this->RootSpacing[2]));
  
  // This assumes 1 ghost layer (blocks are not completed yet so ....
  //block->OriginIndex[0] = this->StandardBlockDimensions[0] * x - 1;
  //block->OriginIndex[1] = this->StandardBlockDimensions[1] * y - 1;
  //block->OriginIndex[2] = this->StandardBlockDimensions[2] * z - 1; 

  // Complete ghost levels if they have been stripped by the reader.
  block->AddBackGhostLevels(this->StandardBlockDimensions);
}

//----------------------------------------------------------------------------
vtkAMRDualGridHelperBlock* vtkAMRDualGridHelperLevel::AddGridBlock(
  int x, int y, int z, 
  vtkImageData* volume)
{
  // Expand the grid array if necessary.
  if (this->Grid == 0 || x < this->GridExtent[0] || x > this->GridExtent[1] ||
      y < this->GridExtent[2] || y > this->GridExtent[3] ||
      z < this->GridExtent[4] || z > this->GridExtent[5])
    { // Reallocate
    int newExt[6];
    newExt[0] = (this->GridExtent[0] < x) ? this->GridExtent[0] : x;
    newExt[1] = (this->GridExtent[1] > x) ? this->GridExtent[1] : x;
    newExt[2] = (this->GridExtent[2] < y) ? this->GridExtent[2] : y;
    newExt[3] = (this->GridExtent[3] > y) ? this->GridExtent[3] : y;
    newExt[4] = (this->GridExtent[4] < z) ? this->GridExtent[4] : z;
    newExt[5] = (this->GridExtent[5] > z) ? this->GridExtent[5] : z;
    int yInc = newExt[1]-newExt[0]+1;
    int zInc = (newExt[3]-newExt[2]+1)*yInc;
    int newSize = zInc*(newExt[5]-newExt[4]+1);
    vtkAMRDualGridHelperBlock** newGrid = new vtkAMRDualGridHelperBlock*[newSize];
    memset(newGrid,0,newSize*sizeof(vtkAMRDualGridHelperBlock*));
    // Copy the blocks over to the new array.
    vtkAMRDualGridHelperBlock** ptr = this->Grid;
    for (int kk = this->GridExtent[4]; kk <= this->GridExtent[5]; ++kk)
      {
      for (int jj = this->GridExtent[2]; jj <= this->GridExtent[3]; ++jj)
        {
        for (int ii = this->GridExtent[0]; ii <= this->GridExtent[1]; ++ii)
          {
          newGrid[ii+(jj*yInc)+(kk*zInc)] = *ptr++;
          }
        }
      }
    memcpy(this->GridExtent, newExt, 6*sizeof(int));
    this->GridIncY = yInc;
    this->GridIncZ = zInc;
    if (this->Grid) { delete [] this->Grid;}
    this->Grid = newGrid;
    }

  vtkAMRDualGridHelperBlock* newBlock = new vtkAMRDualGridHelperBlock();
  newBlock->Image = volume;
  newBlock->Level = this->Level;
  this->Grid[x+(y*this->GridIncY)+(z*this->GridIncZ)] =  newBlock;
  this->Blocks.push_back(newBlock);
  newBlock->GridIndex[0] = x;
  newBlock->GridIndex[1] = y;
  newBlock->GridIndex[2] = z;

  return newBlock;
}


//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::CreateFaces()
{
  int* ext;
  int level, x,y,z;
  vtkAMRDualGridHelperBlock** blockPtr;
  // Start witht the low levels.
  for (level = 0; level < this->GetNumberOfLevels(); ++level)
    {
    blockPtr = this->Levels[level]->Grid;
    ext = this->Levels[level]->GridExtent;
    for (z = ext[4]; z <= ext[5]; ++z)
      {
      for (y = ext[2]; y <= ext[3]; ++y)
        {
        for (x = ext[0]; x <= ext[1]; ++x)
          {
          // Look through all lower levels for existing faces.
          // Lower levels dominate.
          this->FindExistingFaces(*blockPtr, level, x, y, z);
          // Create faces that have not been used yet
          this->Levels[level]->CreateBlockFaces(*blockPtr, x, y, z);
          ++blockPtr;
          }
        }
      }
    }
}

//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::FindExistingFaces(
  vtkAMRDualGridHelperBlock* block,
  int level, int x, int y, int z)
{
  if (block == 0)
    {
    return;
    }
    
  vtkAMRDualGridHelperBlock* block2;
  int ii, jj, kk;
  int lowerLevel;
  int levelDifference;
  int ext[6];
  int ext2[6]; // Extent of grid in lower level.
  int ext3[6]; // Convert ext back to orginal level
  ext[0] = x; ext[1] = x+1;
  ext[2] = y; ext[3] = y+1;
  ext[4] = z; ext[5] = z+1;
  
  // We only really need to check one level lower.
  // anything else is not allowed.
  // But what about edges and corners?
  // The degenerate cell trick should work for any level difference.
  //.(But our logic assumes 1 level difference.)
  // We will have to record the level of degeneracy.
  // Just one level for now.
  for (lowerLevel = 0; lowerLevel < level; ++lowerLevel)
    {
    levelDifference = level - lowerLevel;
    for (ii = 0; ii < 6; ++ii)
      {
      ext2[ii] = ext[ii] >> levelDifference;
      ext3[ii] = ext2[ii] << levelDifference;
      }
    // If we convert index to lower level and then back and it does
    // not change then, the different level blocks share a face.
    for (kk = -1; kk <= 1; ++kk)
      {
      for (jj = -1; jj <= 1; ++jj)
        {
        for (ii = -1; ii <= 1; ++ii)
          {
          // Somewhat convoluted logic to determnine if face/edge/corner is external.
          if ((ii != -1 || ext3[0] == ext[0]) && (ii != 1 || ext3[1] == ext[1]) &&
              (jj != -1 || ext3[2] == ext[2]) && (jj != 1 || ext3[3] == ext[3]) &&
              (kk != -1 || ext3[4] == ext[4]) && (kk != 1 || ext3[5] == ext[5]))
            { // This face/edge/corner is external and may have a neighbor in the lower resolution.
            // Special handling for face structures.
            // Face structures are used for seeding connectivity between blocks.
            // Note that ext2[0] is now equal to ext2[1] (an the same for the other axes too).
            block2 = this->Levels[lowerLevel]->GetGridBlock(ext2[0], ext2[2], ext2[4]);
            if (block2)
              {
              if (ii == -1 && jj == 0 && kk == 0)
                {
                block->SetFace(0, block2->Faces[1]);
                }
              else if (ii == 1 && jj == 0 && kk == 0)
                {
                block->SetFace(1, block2->Faces[0]);
                }
              else if (jj == -1 && ii == 0 && kk == 0)
                {
                block->SetFace(2, block2->Faces[3]);
                }
              else if (jj == 1 && ii == 0 && kk == 0)
                {
                block->SetFace(3, block2->Faces[2]);
                }
              else if (kk == -1 && ii == 0 && jj == 0)
                {
                block->SetFace(4, block2->Faces[5]);
                }
              else if (kk == 1 && ii == 0 && jj == 0)
                {
                block->SetFace(5, block2->Faces[4]);
                }
              }
            }
          }
        }
      }
    }
}



//----------------------------------------------------------------------------
// Negotiate which blocks will be responsible for generating which shared 
// regions.  Higher levels dominate lower levels.  We also set the
// neighbor bits which indicate which cells/points become degenerate.
void vtkAMRDualGridHelper::AssignSharedRegions()
{
  int* ext;
  int level, x,y,z;
  vtkAMRDualGridHelperBlock** blockPtr;

  // Start with the highest levels and work down.
  for (level = this->GetNumberOfLevels()-1; level >= 0; --level)
    {
    blockPtr = this->Levels[level]->Grid;
    ext = this->Levels[level]->GridExtent;
    // Loop through all blocks in the grid.
    // If blocks remembered their grid location, this would be easier.
    // Blocks now remeber there grid xyz locations but it is good that we
    // loop over the grid.  I assume that every process visits
    // to blocks in the same order.  The 1-d block array may
    // not have the same order on all processes, but this
    // grid traversal does.
    for (z = ext[4]; z <= ext[5]; ++z)
      {
      for (y = ext[2]; y <= ext[3]; ++y)
        {
        for (x = ext[0]; x <= ext[1]; ++x)
          {
          if (*blockPtr)
            {
            this->AssignBlockSharedRegions(*blockPtr, level, x, y, z);
            }
          ++blockPtr;
          }
        }
      }
    }
}
void vtkAMRDualGridHelper::AssignBlockSharedRegions(
  vtkAMRDualGridHelperBlock* block, int blockLevel,
  int blockX, int blockY, int blockZ)
{
  int degeneracyLevel;
  // Loop though all the regions.
  int rx, ry, rz;
  for (rz = -1; rz <= 1; ++rz)
    {
    for (ry = -1; ry <= 1; ++ry)
      {
      for (rx = -1; rx <= 1; ++rx)
        {
        if ((rx || ry || rz) && (block->RegionBits[rx+1][ry+1][rz+1] & vtkAMRRegionBitOwner))
          { // A face/edge/corner region and it has not been taken yet.
          degeneracyLevel = this->ClaimBlockSharedRegion(block, blockX,blockY,blockZ, rx,ry,rz);
          // I am using the first 7 bits to store the degenacy level difference.
          // The degenerate flag is now a mask.
          if (this->EnableDegenerateCells && degeneracyLevel < blockLevel)
            {
            unsigned char levelDiff = (unsigned char)(blockLevel - degeneracyLevel);
            if ((vtkAMRRegionBitsDegenerateMask & levelDiff) != levelDiff)
              { // Extreme level difference.
              vtkGenericWarningMacro("Could not encode level difference.");
              }
            block->RegionBits[rx+1][ry+1][rz+1] 
              = vtkAMRRegionBitOwner + (vtkAMRRegionBitsDegenerateMask & levelDiff);
            }
          }
        }
      }
    }
}
// Returns the grid level that the points in this region should be
// projected to.  This will cause these cells to become degenerate
// (Pyramids wedges ...) and nicely transition between levels.
int vtkAMRDualGridHelper::ClaimBlockSharedRegion(
  vtkAMRDualGridHelperBlock* block, 
  int blockX,  int blockY,  int blockZ,
  int regionX, int regionY, int regionZ)
{
  vtkAMRDualGridHelperBlock* neighborBlock;
  vtkAMRDualGridHelperBlock* bestBlock;
  int tx,ty,tz;
  int dist, bestDist;
  int bestLevel;
  int blockLevel = block->Level;
  int startX, startY, startZ, endX, endY, endZ;
  int ix, iy,iz;
  int lowerLevel, levelDifference;
  int lowerX, lowerY, lowerZ;
  int ii;
  int ext1[6]; // Point extent of the single block
  int ext2[6]; // Point extent of block in lower level.
  int ext3[6]; // Extent2 converted back to orginal level.

  ext1[0]=blockX; ext1[1]=blockX+1;
  ext1[2]=blockY; ext1[3]=blockY+1;
  ext1[4]=blockZ; ext1[5]=blockZ+1;

  // This middle of the block is this far from the region.
  // Sort of city block distance.  All region indexes are in [-1,1]
  // the multiplications is effectively computing the absolute value.
  bestDist = regionX*regionX + regionY*regionY + regionZ*regionZ;
  bestLevel = blockLevel;
  bestBlock = block;

  // Loop through all levels (except higher levels) marking
  // this regions as taken.  Higher levels have already claimed
  // their regions so it would be useless to check them.
  for (lowerLevel = blockLevel; lowerLevel >= 0; --lowerLevel)
    {
    levelDifference = blockLevel - lowerLevel;
    for (ii = 0; ii < 6; ++ii)
      {
      ext2[ii] = ext1[ii] >> levelDifference;
      ext3[ii] = ext2[ii] << levelDifference;
      }
    // If we convert index to lower level and then back and it does
    // not change then, the different level blocks share a face.
    // Somewhat convoluted logic to determnine if face/edge/corner is external.
    if ((regionX == -1 && ext3[0] == ext1[0]) || (regionX == 1 && ext3[1] == ext1[1]) ||
        (regionY == -1 && ext3[2] == ext1[2]) || (regionY == 1 && ext3[3] == ext1[3]) ||
        (regionZ == -1 && ext3[4] == ext1[4]) || (regionZ == 1 && ext3[5] == ext1[5]))
      { // This face/edge/corner is on a grid boundary and may have a neighbor in this level.
      // Loop over the blocks that share this region. Faces have 2, edges 4 and corner 8.
      // This was a real pain.  I could not have a loop that would increment 
      // up or down (depending on sign of regionX,regionY,regionZ).
      // Sort start and end to have loop always increment up.
      startX = startY = startZ = 0;
      endX=regionX;    endY=regionY;    endZ=regionZ;
      if (regionX < 0) {startX = regionX; endX = 0;}
      if (regionY < 0) {startY = regionY; endY = 0;}
      if (regionZ < 0) {startZ = regionZ; endZ = 0;}
      for (iz = startZ; iz <= endZ; ++iz)
        {
        for (iy = startY; iy <= endY; ++iy)
          {
          for (ix = startX; ix <= endX; ++ix)
            {
            // Skip the middle (non neighbor).
            if (ix || iy || iz)
              {
              lowerX = (blockX+ix) >> levelDifference;
              lowerY = (blockY+iy) >> levelDifference;
              lowerZ = (blockZ+iz) >> levelDifference;
              neighborBlock = this->Levels[lowerLevel]->GetGridBlock(lowerX,lowerY,lowerZ);
              // Problem. For internal edge ghost. Lower level is direction -1
              // So computation of distance is not correct.
              if (neighborBlock)
                {
                // Mark this face of the block as non boudary.
                if (ix == -1 && iy == 0 && iz == 0)
                  { // Turn off the -x boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 62;
                  // Turn off neighbor boundary bit.  It is not necessary because the
                  // neighbor does not own the region and will not process it.
                  // However, it is confusing when debugging not to have the correct bits set.
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 61;
                  }
                if (ix == 1 && iy == 0 && iz == 0)
                  { // Turn off the x boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 61;
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 62;
                  }
                if (ix == 0 && iy == -1 && iz == 0)
                  { // Turn off the -y boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 59;
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 55;
                  }
                if (ix == 0 && iy == 1 && iz == 0)
                  { // Turn off the y boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 55;
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 59;
                  }
                if (ix == 0 && iy == 0 && iz == -1)
                  { // Turn off the -z boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 47;
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 31;
                  }
                if (ix == 0 && iy == 0 && iz == 1)
                  { // Turn off the z boundary bit.
                  block->BoundaryBits = block->BoundaryBits & 31;
                  neighborBlock->BoundaryBits = neighborBlock->BoundaryBits & 47;
                  }

                // Vote for degeneracy level.
                if (this->EnableDegenerateCells)
                  {
                  // Now remove the neighbors owner bit for this region.
                  // How do we find the region in the neighbor?
                  // Remove assignment for this region from neighbor 
                  neighborBlock->RegionBits[regionX-ix-ix+1][regionY-iy-iy+1][regionZ-iz-iz+1] = 0;
                  tx=regionX-ix;    ty=regionY-iy;    tz=regionZ-iz; // all should be in [-1,1]
                  dist = tx*tx + ty*ty + tz*tz;
                  if (dist < bestDist)
                    {
                    bestLevel = lowerLevel;
                    bestDist = dist;
                    bestBlock = neighborBlock;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

  // If the region is degenerate and points have to be moved 
  // to a lower level grid, tehn we have to copy the 
  // volume fractions from the lower level grid too.
  if (this->EnableDegenerateCells && bestLevel < blockLevel)
    {
    if (block->Image == 0 || bestBlock->Image == 0)
      { // Deal with remote blocks later.
      // Add the pair of blocks to a queue to copy when we get the data.
      vtkAMRDualGridHelperDegenerateRegion dreg;
      dreg.ReceivingBlock = block;
      dreg.ReceivingRegion[0] = regionX;
      dreg.ReceivingRegion[1] = regionY;
      dreg.ReceivingRegion[2] = regionZ;
      dreg.SourceBlock = bestBlock;
      if ( ! this->SkipGhostCopy)
        {
        this->DegenerateRegionQueue.push_back(dreg);
        }
      }
    else
      {
      this->CopyDegenerateRegionBlockToBlock(regionX,regionY,regionZ, bestBlock, block);
      }
    }

  return bestLevel;
}



// Just a hack to test an assumption.
// This can be removed once we determine hor the ghost values behave across
// level changes.
static int vtkDualGridHelperCheckAssumption = 0;
static int vtkDualGridHelperSkipGhostCopy = 0;


// The following three methods are all similar and should be reworked so that
// the share more code.  One possibility to to have block to block copy
// always go through an intermediate buffer (as if is were remote).
// THis should not add much overhead to the copy.

template <class T>
void vtkDualGridHelperCopyBlockToBlock(T* ptr, T* lowerPtr, int ext[6], int levelDiff,
                                       int yInc, int zInc,
                                       int highResBlockOriginIndex[3],
                                       int lowResBlockOriginIndex[3])
{
  T val;
  T *xPtr, *yPtr, *zPtr;
  zPtr = ptr + ext[0]+yInc*ext[2] + zInc*ext[4];
  int lx, ly, lz; // x,y,z converted to lower grid indexes.
  for (int z = ext[4]; z <= ext[5]; ++z)
    {
    lz = ((z+highResBlockOriginIndex[2]) >> levelDiff) - lowResBlockOriginIndex[2];
    yPtr = zPtr;
    for (int y = ext[2]; y <= ext[3]; ++y)
      {
      ly = ((y+highResBlockOriginIndex[1]) >> levelDiff) - lowResBlockOriginIndex[1];
      xPtr = yPtr;
      for (int x = ext[0]; x <= ext[1]; ++x)
        {
        lx = ((x+highResBlockOriginIndex[0]) >> levelDiff) - lowResBlockOriginIndex[0];
        val = lowerPtr[lx + ly*yInc + lz*zInc];
        // Lets see if our assumption about ghost values is correct.
        if (vtkDualGridHelperCheckAssumption && vtkDualGridHelperSkipGhostCopy && *xPtr != val)
          {
          vtkGenericWarningMacro("Ghost assumption incorrect.  Seams may result.");
          // Report issue once per execution.
          vtkDualGridHelperCheckAssumption = 0;
          }
        *xPtr = val;
        xPtr++;
        }
      yPtr += yInc;
      }
    zPtr += zInc;
    }
}
// Ghost volume fraction values are not consistent across levels.
// We need the degenerate high-res volume fractions
// to match corresponding values in low res-blocks.
// This method copies low-res values to high-res ghost blocks.
void vtkAMRDualGridHelper::CopyDegenerateRegionBlockToBlock(
  int regionX, int regionY, int regionZ,
  vtkAMRDualGridHelperBlock* lowResBlock,
  vtkAMRDualGridHelperBlock* highResBlock)
{
  int levelDiff = highResBlock->Level - lowResBlock->Level;
  if (levelDiff == 0)
    { // double check.
    return;
    }
  if (levelDiff < 0)
    { // We added the levels in the wrong order.
    vtkGenericWarningMacro("Reverse level change.");
    return;
    }
  if (highResBlock->CopyFlag == 0)
    { // We cannot modify our input.
    vtkImageData* copy = vtkImageData::New();
    // We only really need to deep copy the one volume fraction array.
    // All others can be shallow copied.
    copy->DeepCopy(highResBlock->Image);
    highResBlock->Image = copy;
    highResBlock->CopyFlag = 1;
    }

  // Now copy low resolution into highresolution ghost layer.
  // For simplicity loop over all three axes (one will be degenerate).
  vtkDataArray *da = highResBlock->Image->GetCellData()->GetArray(this->ArrayName);
  if (da == 0) {return;}
  void *ptr = da->GetVoidPointer(0);
  int   daType = da->GetDataType();

  // Lower block pointer
  da = lowResBlock->Image->GetCellData()->GetArray(this->ArrayName);
  if (da == 0) {return;}
  if (da->GetDataType() != daType)
    {
    vtkGenericWarningMacro("Type mismatch.");
    return;
    }
  void *lowerPtr = da->GetVoidPointer(0);

  // Get the extent of the high-res region we are replacing with values from the neighbor.
  int ext[6];
  ext[0] = ext[2] = ext[4] = 0;
  ext[1] = this->StandardBlockDimensions[0] + 1; // Add ghost layers back in.
  ext[3] = this->StandardBlockDimensions[1] + 1; // Add ghost layers back in.
  ext[5] = this->StandardBlockDimensions[2] + 1; // Add ghost layers back in.
  
  // Test an assumption used in this method.
  if (ext[0] != 0 || ext[2] != 0 || ext[4] != 0)
    {
    vtkGenericWarningMacro("Expecting min extent to be 0.");
    return;
    }
  int yInc = ext[1]-ext[0]+1;
  int zInc = yInc*(ext[5]-ext[4]+1);


  switch (regionX)
    {
    case -1:
      ext[1] =  ext[0];   break;
    case 0:
      ++ext[0]; --ext[1]; break;
    case 1:
      ext[0] =  ext[1];   break;
    }
  switch (regionY)
    {
    case -1:
      ext[3] =  ext[2];   break;
    case 0:
      ++ext[2]; --ext[3]; break;
    case 1:
      ext[2] =  ext[3];   break;
    }
  switch (regionZ)
    {
    case -1:
      ext[5] =  ext[4];   break;
    case 0:
      ++ext[4]; --ext[5]; break;
    case 1:
      ext[4] =  ext[5];   break;
    }

  vtkDualGridHelperSkipGhostCopy =  this->SkipGhostCopy;
  // Assume all blocks have the same extent.
  switch (daType)
    {
    vtkTemplateMacro(vtkDualGridHelperCopyBlockToBlock(
      static_cast<VTK_TT *>(ptr),
      static_cast<VTK_TT *>(lowerPtr),
      ext, levelDiff, yInc, zInc,
      highResBlock->OriginIndex,
      lowResBlock->OriginIndex));
    default:
      vtkGenericWarningMacro("Execute: Unknown ScalarType");
      return;
    }
}
// Ghost volume fraction values are not consistent across levels.
// We need the degenerate high-res volume fractions
// to match corresponding values in low res-blocks.
// This method copies low-res values to high-res ghost blocks.
template <class T>
void* vtkDualGridHelperCopyBlockToMessage(T* messagePtr, T* lowerPtr, 
                                          int ext[6], int yInc, int zInc)
{
  // Loop over regions values (cells/dual points) and
  // copy into message.
  for (int z = ext[4]; z <= ext[5]; ++z)
    {
    for (int y = ext[2]; y <= ext[3]; ++y)
      {
      for (int x = ext[0]; x <= ext[1]; ++x)
        {
        *messagePtr++ = lowerPtr[x + y*yInc + z*zInc];
        }
      }
    }
  return messagePtr;
}
void* vtkAMRDualGridHelper::CopyDegenerateRegionBlockToMessage(
  int regionX, int regionY, int regionZ,
  vtkAMRDualGridHelperBlock* lowResBlock,
  vtkAMRDualGridHelperBlock* highResBlock,
  void* messagePtr)
{    
  int levelDiff = highResBlock->Level - lowResBlock->Level;
  if (levelDiff == 0)
    { // double check.
    return messagePtr;
    }
  if (levelDiff < 0)
    { // We added the levels in the wrong order.
    vtkGenericWarningMacro("Reverse level change.");
    return messagePtr;
    }
  // Lower block pointer
  vtkDataArray* da;
  da = lowResBlock->Image->GetCellData()->GetArray(this->ArrayName);
  if (da == 0) {return messagePtr;}
  int daType = da->GetDataType();
  void *lowerPtr = da->GetVoidPointer(0);

  // Get the extent of the high-res region we are replacing with values from the neighbor.
  int ext[6];
  ext[0] = ext[2] = ext[4] = 0;
  ext[1] = this->StandardBlockDimensions[0] + 1; // Add ghost layers back in.
  ext[3] = this->StandardBlockDimensions[1] + 1; // Add ghost layers back in.
  ext[5] = this->StandardBlockDimensions[2] + 1; // Add ghost layers back in.
  int yInc = ext[1]-ext[0]+1;
  int zInc = yInc*(ext[5]-ext[4]+1);

  switch (regionX)
    {
    case -1:
      ext[1] =  ext[0];   break;
    case 0:
      ++ext[0]; --ext[1]; break;
    case 1:
      ext[0] =  ext[1];   break;
    }
  switch (regionY)
    {
    case -1:
      ext[3] =  ext[2];   break;
    case 0:
      ++ext[2]; --ext[3]; break;
    case 1:
      ext[2] =  ext[3];   break;
    }
  switch (regionZ)
    {
    case -1:
      ext[5] =  ext[4];   break;
    case 0:
      ++ext[4]; --ext[5]; break;
    case 1:
      ext[4] =  ext[5];   break;
    }

  // Convert to the extent of the low resolution source block.
  ext[0] = ((ext[0]+highResBlock->OriginIndex[0]) >> levelDiff) - lowResBlock->OriginIndex[0];
  ext[1] = ((ext[1]+highResBlock->OriginIndex[0]) >> levelDiff) - lowResBlock->OriginIndex[0];
  ext[2] = ((ext[2]+highResBlock->OriginIndex[1]) >> levelDiff) - lowResBlock->OriginIndex[1];
  ext[3] = ((ext[3]+highResBlock->OriginIndex[1]) >> levelDiff) - lowResBlock->OriginIndex[1];
  ext[4] = ((ext[4]+highResBlock->OriginIndex[2]) >> levelDiff) - lowResBlock->OriginIndex[2];
  ext[5] = ((ext[5]+highResBlock->OriginIndex[2]) >> levelDiff) - lowResBlock->OriginIndex[2];

  // Assume all blocks have the same extent.
  switch (daType)
    {
    vtkTemplateMacro(messagePtr=vtkDualGridHelperCopyBlockToMessage(
                  static_cast<VTK_TT *>(messagePtr),
                  static_cast<VTK_TT *>(lowerPtr),
                  ext, yInc, zInc));
    default:
      vtkGenericWarningMacro("Execute: Unknown ScalarType");
      return messagePtr;
    }

  return messagePtr;
}
// Take the low res message and copy to the high res block.
template <class T>
void* vtkDualGridHelperCopyMessageToBlock(T* ptr, T* messagePtr, 
                                       int ext[6],int messageExt[6],int levelDiff,
                                       int yInc, int zInc, 
                                       int highResBlockOriginIndex[3],
                                       int lowResBlockOriginIndex[3])
{
  int messageIncY = messageExt[1]-messageExt[0]+1;
  int messageIncZ = messageIncY * (messageExt[3]-messageExt[2]+1);
  // Loop over regions values (cells/dual points).
  T *xPtr, *yPtr, *zPtr;
  zPtr = ptr + ext[0]+yInc*ext[2] + zInc*ext[4];
  int lx, ly, lz; // x,y,z converted to lower grid indexes.
  for (int z = ext[4]; z <= ext[5]; ++z)
    {
    lz = ((z+highResBlockOriginIndex[2]) >> levelDiff) - lowResBlockOriginIndex[2] - messageExt[4];
    yPtr = zPtr;
    for (int y = ext[2]; y <= ext[3]; ++y)
      {
      ly = ((y+highResBlockOriginIndex[1]) >> levelDiff) - lowResBlockOriginIndex[1] - messageExt[2];
      xPtr = yPtr;
      for (int x = ext[0]; x <= ext[1]; ++x)
        {
        lx = ((x+highResBlockOriginIndex[0]) >> levelDiff) - lowResBlockOriginIndex[0] - messageExt[0];
        *xPtr = messagePtr[lx + ly*messageIncY + lz*messageIncZ];
        xPtr++;
        }
      yPtr += yInc;
      }
    zPtr += zInc;
    }
  return messagePtr + (messageIncZ*(messageExt[5]-messageExt[4]+1));
}
void* vtkAMRDualGridHelper::CopyDegenerateRegionMessageToBlock(
  int regionX, int regionY, int regionZ,
  vtkAMRDualGridHelperBlock* lowResBlock,
  vtkAMRDualGridHelperBlock* highResBlock,
  void* messagePtr)
{
  int levelDiff = highResBlock->Level - lowResBlock->Level;
  if (levelDiff == 0)
    { // double check.
    return messagePtr;
    }
  if (levelDiff < 0)
    { // We added the levels in the wrong order.
    vtkGenericWarningMacro("Reverse level change.");
    return messagePtr;
    }
  if (highResBlock->CopyFlag == 0)
    { // We cannot modify our input.
    vtkImageData* copy = vtkImageData::New();
    copy->DeepCopy(highResBlock->Image);
    highResBlock->Image = copy;
    highResBlock->CopyFlag = 1;
    }

  // Now copy low resolution into highresolution ghost layer.
  // For simplicity loop over all three axes (one will be degenerate).
  vtkDataArray *da = highResBlock->Image->GetCellData()->GetArray(this->ArrayName);
  if (da == 0) {return messagePtr;}
  int daType = da->GetDataType();
  void *ptr = da->GetVoidPointer(0);

  // Get the extent of the high-res region we are replacing with values from the neighbor.
  int ext[6];
  ext[0] = ext[2] = ext[4] = 0;
  ext[1] = this->StandardBlockDimensions[0] + 1; // Add ghost layers back in.
  ext[3] = this->StandardBlockDimensions[1] + 1; // Add ghost layers back in.
  ext[5] = this->StandardBlockDimensions[2] + 1; // Add ghost layers back in.
  
  // Test an assumption used in this method.
  if (ext[0] != 0 || ext[2] != 0 || ext[4] != 0)
    {
    vtkGenericWarningMacro("Expecting min extent to be 0.");
    return messagePtr;
    }
  int yInc = ext[1]-ext[0]+1;
  int zInc = yInc*(ext[5]-ext[4]+1);
  
  switch (regionX)
    {
    case -1:
      ext[1] =  ext[0];   break;
    case 0:
      ++ext[0]; --ext[1]; break;
    case 1:
      ext[0] =  ext[1];   break;
    }
  switch (regionY)
    {
    case -1:
      ext[3] =  ext[2];   break;
    case 0:
      ++ext[2]; --ext[3]; break;
    case 1:
      ext[2] =  ext[3];   break;
    }
  switch (regionZ)
    {
    case -1:
      ext[5] =  ext[4];   break;
    case 0:
      ++ext[4]; --ext[5]; break;
    case 1:
      ext[4] =  ext[5];   break;
    }

  // Convert to the extent of the low resolution source block.
  int messageExt[6];
  messageExt[0] = ((ext[0]+highResBlock->OriginIndex[0]) >> levelDiff) - lowResBlock->OriginIndex[0];
  messageExt[1] = ((ext[1]+highResBlock->OriginIndex[0]) >> levelDiff) - lowResBlock->OriginIndex[0];
  messageExt[2] = ((ext[2]+highResBlock->OriginIndex[1]) >> levelDiff) - lowResBlock->OriginIndex[1];
  messageExt[3] = ((ext[3]+highResBlock->OriginIndex[1]) >> levelDiff) - lowResBlock->OriginIndex[1];
  messageExt[4] = ((ext[4]+highResBlock->OriginIndex[2]) >> levelDiff) - lowResBlock->OriginIndex[2];
  messageExt[5] = ((ext[5]+highResBlock->OriginIndex[2]) >> levelDiff) - lowResBlock->OriginIndex[2];

  switch (daType)
    {
    vtkTemplateMacro(messagePtr=vtkDualGridHelperCopyMessageToBlock(
                  static_cast<VTK_TT *>(ptr),
                  static_cast<VTK_TT *>(messagePtr),
                  ext, messageExt, levelDiff, yInc, zInc,
                  highResBlock->OriginIndex,
                  lowResBlock->OriginIndex));
    default:
      vtkGenericWarningMacro("Execute: Unknown ScalarType");
      return messagePtr;
    }
  return messagePtr;    
}



//----------------------------------------------------------------------------
// I am assuming that each block has the same extent.  If boundary ghost
// cells are removed by the reader, then I will add them back as the first
// step of initialization.
void vtkAMRDualGridHelper::ProcessDegenerateRegionQueue()
{
  if (this->Controller == 0 || this->SkipGhostCopy)
    {
    return;
    }
  int numProcs = this->Controller->GetNumberOfProcesses();
  int myProc = this->Controller->GetLocalProcessId();
  int procIdx;
  
  for (procIdx = 0; procIdx < numProcs; ++procIdx)
    {
    // To avoid blocking.
    // Lower processes send first and receive second.
    // Higher processes receive first and send second.
    if (procIdx < myProc)
      {
      this->SendDegenerateRegionsFromQueue(procIdx, myProc);
      this->ReceiveDegenerateRegionsFromQueue(procIdx, myProc);
      }
    else if (procIdx > myProc)
      {
      this->ReceiveDegenerateRegionsFromQueue(procIdx, myProc);
      this->SendDegenerateRegionsFromQueue(procIdx, myProc);
      }
    }
}


//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::SendDegenerateRegionsFromQueue(int remoteProc, int localProc)
{
  // Find the length of the message.
  vtkAMRDualGridHelperDegenerateRegion* region;
  int messageLength = 0;
  int queueLength = (int)(this->DegenerateRegionQueue.size());
  int queueIdx;
  
  // Each region is actually either 1/4 of a face, 1/2 of and edge or a corner.

  // Note: In order to minimize communication, I am rellying heavily on the fact 
  // that the queue will be the same on all processes.  Message/region lengths
  // are computed implicitely.

  for (queueIdx = 0; queueIdx < queueLength; ++queueIdx)
    {
    region = &(this->DegenerateRegionQueue[queueIdx]);    
    if ( region->ReceivingBlock->ProcessId == remoteProc &&
         region->SourceBlock->ProcessId == localProc)
      { // We are assuming that the queue is ordered consistently on all processes.
      // This avoids having to send the block indexes along with the data.
      // The extra memory is no big deal, but it is a pain to marshal integers
      // into a single message.
      int regionSize = 1;
      if (region->ReceivingRegion[0] == 0)
        {
        // Note:  In rare cases, level difference can be larger than 1.
        // This will reserve to much memory with no real harm done.
        // Half the root dimensions, ghost layers not included.
        // Ghost layers are handled by separate edge and corner regions.
        regionSize *= (this->StandardBlockDimensions[0] >> 1);
        }
      if (region->ReceivingRegion[1] == 0)
        {
        regionSize *= (this->StandardBlockDimensions[1] >> 1);
        }
      if (region->ReceivingRegion[2] == 0)
        {
        regionSize *= (this->StandardBlockDimensions[2] >> 1);
        }
      messageLength += regionSize * this->DataTypeSize;
      }
    }
  this->AllocateMessageBuffer(messageLength * sizeof(unsigned char));
  // Now copy the layers into the message buffer.
  void* messagePtr = (void*)(this->MessageBuffer);
  for (queueIdx = 0; queueIdx < queueLength; ++queueIdx)
    {
    region = &(this->DegenerateRegionQueue[queueIdx]);
    if ( region->ReceivingBlock->ProcessId == remoteProc &&
         region->SourceBlock->ProcessId == localProc)
      {
      messagePtr = this->CopyDegenerateRegionBlockToMessage(
                        region->ReceivingRegion[0],
                        region->ReceivingRegion[1],
                        region->ReceivingRegion[2],
                        region->SourceBlock,
                        region->ReceivingBlock,
                        messagePtr);
      }
    }

  // Send the message
  this->Controller->Send(this->MessageBuffer, 
                         messageLength, remoteProc, 879015);
}

//----------------------------------------------------------------------------
void vtkAMRDualGridHelper::ReceiveDegenerateRegionsFromQueue(int remoteProc, int localProc)
{
  // Find the length of the message.
  vtkAMRDualGridHelperDegenerateRegion* region;
  int messageLength = 0;
  int queueLength = (int)(this->DegenerateRegionQueue.size());
  int queueIdx;
  
  // Each region is actually either 1/4 of a face, 1/2 of and edge or a corner.

  // Note: In order to minimize communication, I am rellying heavily on the fact 
  // that the queue will be the same on all processes.  Message/region lengths
  // are computed implicitely.

  // Compute the message length we expect to receive.
  for (queueIdx = 0; queueIdx < queueLength; ++queueIdx)
    {
    region = &(this->DegenerateRegionQueue[queueIdx]);
    if ( region->ReceivingBlock->ProcessId == localProc &&
         region->SourceBlock->ProcessId == remoteProc)
      { // We are assuming that the queue is ordered consistently on all processes.
      // This avoids having to send the block indexes along with the data.
      // The extra memory is no big deal, but it is a pain to marshal integers
      // into a single message.
      int regionSize = 1;
      if (region->ReceivingRegion[0] == 0)
        {
        // Half the root dimensions, ghost layers not included.
        // Ghost layers are handled by separate edge and corner regions.
        regionSize *= (this->StandardBlockDimensions[0] >> 1);
        }
      if (region->ReceivingRegion[1] == 0)
        {
        regionSize *= (this->StandardBlockDimensions[1] >> 1);
        }
      if (region->ReceivingRegion[2] == 0)
        {
        regionSize *= (this->StandardBlockDimensions[2] >> 1);
        }
      messageLength += regionSize * this->DataTypeSize;
      }
    }

  // Receive the message.
  this->AllocateMessageBuffer(messageLength * sizeof(unsigned char));
  unsigned char* message = this->MessageBuffer;
  this->Controller->Receive(message, messageLength, remoteProc, 879015);
  
  // Now copy the regions in the message into thelocal blocks.
  void* messagePtr = (void*)(this->MessageBuffer);
  for (queueIdx = 0; queueIdx < queueLength; ++queueIdx)
    {
    region = &(this->DegenerateRegionQueue[queueIdx]);
    if ( region->ReceivingBlock->ProcessId == localProc &&
         region->SourceBlock->ProcessId == remoteProc)
      {
      messagePtr = this->CopyDegenerateRegionMessageToBlock(
                        region->ReceivingRegion[0],
                        region->ReceivingRegion[1],
                        region->ReceivingRegion[2],
                        region->SourceBlock,
                        region->ReceivingBlock,
                        messagePtr);
      }
    }
}


// We need to know:
// The number of levels (to make the level structures)
// Global origin, RootSpacing, StandarBlockSize (to convert block extent to grid extent)
// Add all blocks to the level/grids and create faces along the way.


// Note:
// Reader crops invalid ghost cells off boundary blocks.
// Some blocks will have smaller extents!
//----------------------------------------------------------------------------
// All processes must share a common origin.
// Returns the total number of blocks in all levels (this process only).
// This computes:  GlobalOrigin, RootSpacing and StandardBlockDimensions.
// StandardBlockDimensions are the size of blocks without the extra
// overlap layer put on by spyplot format.
// RootSpacing is the spacing that blocks in level 0 would have.
// GlobalOrigin is choosen so that there are no negative extents and
// base extents (without overlap/ghost buffer) lie on grid 
// (i.e.) the min base extent must be a multiple of the standardBlockDimesions.
// The array name is the cell array that is being processed by the filter.
// Ghost values have to be modified at level changes.  It could be extended to
// process multiple arrays.
int vtkAMRDualGridHelper::Initialize(vtkHierarchicalBoxDataSet* input, 
                                     const char* arrayName)
{
  int blockId, numBlocks;
  int numLevels = input->GetNumberOfLevels();

  vtkDualGridHelperCheckAssumption = 1;
  this->SetArrayName(arrayName);

  // Create the level objects.
  this->Levels.reserve(numLevels);
  for (int ii = 0; ii < numLevels; ++ii)
    {
    vtkAMRDualGridHelperLevel* tmp = new vtkAMRDualGridHelperLevel;
    tmp->Level = ii;
    this->Levels.push_back(tmp);
    }

  this->ComputeGlobalMetaData(input);

  // Add all of the blocks
  for (int level = 0; level < numLevels; ++level)
    {
    numBlocks = input->GetNumberOfDataSets(level);
    for (blockId = 0; blockId < numBlocks; ++blockId)
      {
      vtkAMRBox box;
      vtkImageData* image = input->GetDataSet(level,blockId,box);
      if (image)
        {
        this->AddBlock(level, image);
        }
      }
    }
  // All processes will have all blocks (but not image data).
  this->ShareBlocks();
  
  // Plan for meshing between blocks.
  this->AssignSharedRegions();
  
  // Copy regions on level boundaries between processes.
  this->ProcessDegenerateRegionQueue();
  
  // Setup faces for seeding connectivity between blocks.
  //this->CreateFaces();
  
  return VTK_OK;
}
void vtkAMRDualGridHelper::ShareBlocks()
{
  if (this->Controller == 0 || this->Controller->GetNumberOfProcesses() == 1)
    {
    return;
    }

  // I could use alltoN ....
  // For now: collect to 0, and broadcast back.
  int procIdx;
  int myProc = this->Controller->GetLocalProcessId();
  if (myProc == 0)
    { // Collect / Receive blocs from all other processes.
    int numProcs = this->Controller->GetNumberOfProcesses();
    for (procIdx = 1; procIdx < numProcs; ++procIdx)
      {
      this->ReceiveBlocks(procIdx);
      }
    // Broadcast / send back.
    for (procIdx = 1; procIdx < numProcs; ++procIdx)
      {
      this->SendBlocks(procIdx, myProc);
      }
    }
  else
    {
    this->SendBlocks(0, myProc);
    this->ReceiveBlocks(0);
    }
}
void vtkAMRDualGridHelper::AllocateMessageBuffer(int maxSize)
{
  if (this->MessageBufferLength < maxSize)
    {
    if (this->MessageBuffer)
      {
      delete [] this->MessageBuffer;
      }
    }
  this->MessageBufferLength = maxSize + 100; // Extra to avoid reallocating.
  this->MessageBuffer = new unsigned char[this->MessageBufferLength];
}
void vtkAMRDualGridHelper::ReceiveBlocks(int remoteProc)
{
  int messageLength;
  this->Controller->Receive(&messageLength, 1, remoteProc, 87344879);
  this->AllocateMessageBuffer(messageLength*sizeof(int));
  int* message = (int*)(this->MessageBuffer);
  this->Controller->Receive(message, messageLength, remoteProc, 87344880);
  
  // Now read the message.
  int x, y, z;
  int blockProc = remoteProc;
  int* messagePtr = message;
  int numLevels = *messagePtr++;
  for (int levelIdx = 0; levelIdx < numLevels; ++levelIdx)
    {
    int numBlocks = *messagePtr++;
    vtkAMRDualGridHelperLevel* level;
    level = this->Levels[levelIdx];
    for (int blockIdx = 0; blockIdx < numBlocks; ++blockIdx)
      {
      x = *messagePtr++;
      y = *messagePtr++;
      z = *messagePtr++;
      if (remoteProc == 0)
        {
        blockProc = *messagePtr++;
        }
      vtkAMRDualGridHelperBlock* block = level->AddGridBlock(x,y,z, 0);
      block->ProcessId = blockProc;
      
      block->OriginIndex[0] = this->StandardBlockDimensions[0] * x - 1;
      block->OriginIndex[1] = this->StandardBlockDimensions[1] * y - 1;
      block->OriginIndex[2] = this->StandardBlockDimensions[2] * z - 1;      
      }
    }
}
void vtkAMRDualGridHelper::SendBlocks(int remoteProc, int localProc)
{
  // Marshal the procs.
  // I will try to be as smart about reducing the message size.
  // This is getting complex enough that I should have just used AllToN.
  // locaProc != 0
  // numlevels, level0NumBlocks,(gridx,gridy,gridz,...,level1NumBlocks,...)
  // localProc == 0
  // numlevels, level0NumBlocks,(gridx,gridy,gridz,proc,...,level1NumBlocks,...)
  int numBlocks;
  vtkAMRDualGridHelperBlock* block;
  int messageLength = 1; // One int for the number of levels.
  int numLevels = this->GetNumberOfLevels();
  for (int levelIdx = 0; levelIdx < numLevels; ++levelIdx)
    {
    // One int for the number of blocks in this level.
    ++messageLength;
    if (localProc == 0)
      { // x,y,z,proc for each block.
      messageLength += 4* (int)(this->Levels[levelIdx]->Blocks.size());
      }
    else
      { // xyz for each block.
      messageLength += 3* (int)(this->Levels[levelIdx]->Blocks.size());
      }
    }

  this->AllocateMessageBuffer(messageLength * sizeof(int));

  // Now create the message.
  int *message = (int*)(this->MessageBuffer);
  int* messagePtr = message;
  int* numBlocksPtr;
  *messagePtr++ = numLevels;
  for (int levelIdx = 0; levelIdx < numLevels; ++levelIdx)
    {
    int numBlocksSending = 0;
    numBlocks = (int)(this->Levels[levelIdx]->Blocks.size());
    // Fill in num blocks later.  Process 0 skips blocks sender already has.
    numBlocksPtr = messagePtr++;
    for (int blockIdx = 0; blockIdx < numBlocks; ++blockIdx)
      {
      block = this->Levels[levelIdx]->Blocks[blockIdx];
      if (block->ProcessId != remoteProc)
        {
        ++numBlocksSending;
        *messagePtr++ = block->GridIndex[0];
        *messagePtr++ = block->GridIndex[1];
        *messagePtr++ = block->GridIndex[2];
        if (localProc == 0)
          {
          *messagePtr++ = block->ProcessId;
          }
        }
      }
    // Now fill in the numBlocks in the message.
    *numBlocksPtr = numBlocksSending;
    }
  // Find the actual message length.
  messageLength = messagePtr - message;

  this->Controller->Send(&messageLength, 1, remoteProc, 87344879);
  this->Controller->Send(message, messageLength, remoteProc, 87344880);
}

void vtkAMRDualGridHelper::ComputeGlobalMetaData(vtkHierarchicalBoxDataSet* input)
{
  // This is a big pain.
  // We have to look through all blocks to get a minimum root origin.
  // The origin must be choosen so there are no negative indexes.
  // Negative indexes would require the use floor or ceiling function instead
  // of simple truncation.
  //  The origin must also lie on the root grid.
  // The big pain is finding the correct origin when we do not know which
  // blocks have ghost layers.  The Spyplot reader strips
  // ghost layers from outside blocks.

  // Overall processes:
  // Find the largest of all block dimensions to compute standard dimensions.
  // Save the largest block information.
  // Find the overall bounds of the data set.
  // Find one of the lowest level blocks to compute origin.

  int numLevels = input->GetNumberOfLevels();
  int numBlocks;
  int blockId;

  int    lowestLevel = 0;
  double lowestSpacing[3];
  double lowestOrigin[3];
  int    lowestDims[3];
  int    largestLevel = 0;
  double largestOrigin[3];
  double largestSpacing[3];
  int    largestDims[3];
  int    largestNumCells;
  
  double globalBounds[6];

  // Temporary variables.
  double spacing[3];
  double bounds[6];
  int cellDims[3];
  int numCells;
  int ext[6];

  largestNumCells = 0;
  globalBounds[0] = globalBounds[2] = globalBounds[4] = VTK_LARGE_FLOAT;
  globalBounds[1] = globalBounds[3] = globalBounds[5] = -VTK_LARGE_FLOAT;
  lowestSpacing[0] = lowestSpacing[1] = lowestSpacing[2] = 0.0;

  this->NumberOfBlocksInThisProcess = 0;
  for (int level = 0; level < numLevels; ++level)
    {
    numBlocks = input->GetNumberOfDataSets(level);
    for (blockId = 0; blockId < numBlocks; ++blockId)
      {
      vtkAMRBox box;
      vtkImageData* image = input->GetDataSet(level,blockId,box);
      if (image)
        {
        ++this->NumberOfBlocksInThisProcess;
        image->GetBounds(bounds);
        // Compute globalBounds.
        if (globalBounds[0] > bounds[0]) {globalBounds[0] = bounds[0];}
        if (globalBounds[1] < bounds[1]) {globalBounds[1] = bounds[1];}
        if (globalBounds[2] > bounds[2]) {globalBounds[2] = bounds[2];}
        if (globalBounds[3] < bounds[3]) {globalBounds[3] = bounds[3];}
        if (globalBounds[4] > bounds[4]) {globalBounds[4] = bounds[4];}
        if (globalBounds[5] < bounds[5]) {globalBounds[5] = bounds[5];}
        image->GetExtent(ext);
        cellDims[0] = ext[1]-ext[0]; // ext is point extent.
        cellDims[1] = ext[3]-ext[2];
        cellDims[2] = ext[5]-ext[4];
        numCells = cellDims[0] * cellDims[1] * cellDims[2];
        // Compute standard block dimensions.
        if (numCells > largestNumCells)
          {
          largestDims[0] = cellDims[0];
          largestDims[1] = cellDims[1];
          largestDims[2] = cellDims[2];
          largestNumCells = numCells;
          image->GetOrigin(largestOrigin);
          image->GetSpacing(largestSpacing);
          largestLevel = level;
          }
        // Find the lowest level block.
        image->GetSpacing(spacing);
        if (spacing[0] > lowestSpacing[0]) // Only test axis 0. Assume others agree.
          { // This is the lowest level block we have encountered.
          image->GetSpacing(lowestSpacing);
          lowestLevel = level;
          image->GetOrigin(lowestOrigin);
          lowestDims[0] = cellDims[0];
          lowestDims[1] = cellDims[1];
          lowestDims[2] = cellDims[2];
          }
        }
      }
    }

  // Send the results to process 0 that will choose the origin ...
  int numProcs = 1;
  int myId = 0;
  
  double dMsg[18];
  int    iMsg[9];
  vtkMultiProcessController* controller = this->Controller;
  if (controller)
    {
    numProcs = controller->GetNumberOfProcesses();
    myId = controller->GetLocalProcessId();
    if (myId > 0)
      { // Send to process 0.
      iMsg[0] = lowestLevel;
      iMsg[1] = largestLevel;
      iMsg[2] = largestNumCells;
      for (int ii= 0; ii < 3; ++ii)
        {
        iMsg[3+ii] = lowestDims[ii];
        iMsg[6+ii] = largestDims[ii];
        dMsg[ii]   = lowestSpacing[ii];
        dMsg[3+ii] = lowestOrigin[ii];
        dMsg[6+ii] = largestOrigin[ii];
        dMsg[9+ii] = largestSpacing[ii];
        dMsg[12+ii] = globalBounds[ii];
        dMsg[15+ii] = globalBounds[ii+3];
        }
      controller->Send(iMsg, 9, 0, 8973432);
      controller->Send(dMsg, 15, 0, 8973432);
      }
    else
      {
      // Collect results from all processes.
      for (int id = 1; id < numProcs; ++id)
        {
        controller->Receive(iMsg, 9, id, 8973432);
        controller->Receive(dMsg, 18, id, 8973432);
        numCells = iMsg[2];
        cellDims[0] = iMsg[6];
        cellDims[1] = iMsg[7];
        cellDims[2] = iMsg[8];
        if (numCells > largestNumCells)
          {
          largestDims[0] = cellDims[0];
          largestDims[1] = cellDims[1];
          largestDims[2] = cellDims[2];
          largestNumCells = numCells;
          largestOrigin[0] = dMsg[6];
          largestOrigin[1] = dMsg[7];
          largestOrigin[2] = dMsg[8];
          largestSpacing[0] = dMsg[9];
          largestSpacing[1] = dMsg[10];
          largestSpacing[2] = dMsg[11];
          largestLevel = iMsg[1];
          }
        // Find the lowest level block.
        spacing[0] = dMsg[0];
        spacing[1] = dMsg[1];
        spacing[2] = dMsg[2];
        if (spacing[0] > lowestSpacing[0]) // Only test axis 0. Assume others agree.
          { // This is the lowest level block we have encountered.
          lowestSpacing[0] = spacing[0];
          lowestSpacing[1] = spacing[1];
          lowestSpacing[2] = spacing[2];
          lowestLevel = iMsg[0];
          lowestOrigin[0] = dMsg[3];
          lowestOrigin[1] = dMsg[4];
          lowestOrigin[2] = dMsg[5];
          lowestDims[0] = iMsg[6];
          lowestDims[1] = iMsg[7];
          lowestDims[2] = iMsg[8];
          }
        if (globalBounds[0] > dMsg[9])  {globalBounds[0] = dMsg[9];}
        if (globalBounds[1] < dMsg[10]) {globalBounds[1] = dMsg[10];}
        if (globalBounds[2] > dMsg[11]) {globalBounds[2] = dMsg[11];}
        if (globalBounds[3] < dMsg[12]) {globalBounds[3] = dMsg[12];}
        if (globalBounds[4] > dMsg[13]) {globalBounds[4] = dMsg[13];}
        if (globalBounds[5] < dMsg[14]) {globalBounds[5] = dMsg[14];}
        }
      }
    }

  if (myId == 0)
    {
    this->StandardBlockDimensions[0] = largestDims[0]-2;
    this->StandardBlockDimensions[1] = largestDims[1]-2;
    this->StandardBlockDimensions[2] = largestDims[2]-2;
    // For 2d case
    if (this->StandardBlockDimensions[2] < 1)
      {
      this->StandardBlockDimensions[2] = 1;
      }        
    this->RootSpacing[0] = lowestSpacing[0] * (1 << (lowestLevel));
    this->RootSpacing[1] = lowestSpacing[1] * (1 << (lowestLevel));
    this->RootSpacing[2] = lowestSpacing[2] * (1 << (lowestLevel));
    
//    DebuggingRootSpacing[0] = this->RootSpacing[0];
//    DebuggingRootSpacing[1] = this->RootSpacing[1];
//    DebuggingRootSpacing[2] = this->RootSpacing[2];
    
    // Find the grid for the largest block.  We assume this block has the
    // extra ghost layers.
    largestOrigin[0] = largestOrigin[0] + largestSpacing[0];
    largestOrigin[1] = largestOrigin[1] + largestSpacing[1];
    largestOrigin[2] = largestOrigin[2] + largestSpacing[2];
    // Convert to the spacing of the blocks.
    largestSpacing[0] *= this->StandardBlockDimensions[0];
    largestSpacing[1] *= this->StandardBlockDimensions[1];
    largestSpacing[2] *= this->StandardBlockDimensions[2];
    // Find the point on the grid closest to the lowest level origin.
    // We do not know if this lowest level block has its ghost layers.
    // Even if the dims are one less that standard, which side is missing
    // the ghost layer!
    int idx[3];
    idx[0] = (int)(floor(0.5 + (lowestOrigin[0]-largestOrigin[0]) / largestSpacing[0]));
    idx[1] = (int)(floor(0.5 + (lowestOrigin[1]-largestOrigin[1]) / largestSpacing[1]));
    idx[2] = (int)(floor(0.5 + (lowestOrigin[2]-largestOrigin[2]) / largestSpacing[2]));
    lowestOrigin[0] = largestOrigin[0] + (double)(idx[0])*largestSpacing[0];
    lowestOrigin[1] = largestOrigin[1] + (double)(idx[1])*largestSpacing[1];
    lowestOrigin[2] = largestOrigin[2] + (double)(idx[2])*largestSpacing[2];
    // OK.  Now we have the grid for the lowest level that has a block.
    // Change the grid to be of the blocks.
    lowestSpacing[0] *= this->StandardBlockDimensions[0];
    lowestSpacing[1] *= this->StandardBlockDimensions[1];
    lowestSpacing[2] *= this->StandardBlockDimensions[2];
    
    // Change the origin so that all indexes will be positive.
    idx[0] = (int)(floor((globalBounds[0]-lowestOrigin[0]) / lowestSpacing[0]));
    idx[1] = (int)(floor((globalBounds[2]-lowestOrigin[1]) / lowestSpacing[1]));
    idx[2] = (int)(floor((globalBounds[4]-lowestOrigin[2]) / lowestSpacing[2]));
    this->GlobalOrigin[0] = lowestOrigin[0] + (double)(idx[0])*lowestSpacing[0];
    this->GlobalOrigin[1] = lowestOrigin[1] + (double)(idx[1])*lowestSpacing[1];
    this->GlobalOrigin[2] = lowestOrigin[2] + (double)(idx[2])*lowestSpacing[2];
    
//    DebuggingGlobalOrigin[0] = this->GlobalOrigin[0];
//    DebuggingGlobalOrigin[1] = this->GlobalOrigin[1];
//    DebuggingGlobalOrigin[2] = this->GlobalOrigin[2];
    
    // Now send these to all the other processes and we are done!
    if (this->Controller)
      {
      for (int ii = 0; ii < 3; ++ii)
        {
        dMsg[ii] = this->GlobalOrigin[ii];
        dMsg[ii+3] = this->RootSpacing[ii];
        dMsg[ii+6] = (double)(this->StandardBlockDimensions[ii]);
        }
      for (int ii = 1; ii < numProcs; ++ii)
        {
        controller->Send(dMsg, 9, ii, 8973439);
        }
      }
    }
  else
    {
    controller->Receive(dMsg, 9, 0, 8973439);
    for (int ii = 0; ii < 3; ++ii)
      {
      this->GlobalOrigin[ii] = dMsg[ii];
      this->RootSpacing[ii] = dMsg[ii+3];
      this->StandardBlockDimensions[ii] = (int)(dMsg[ii+6]);
      }
    }
}


