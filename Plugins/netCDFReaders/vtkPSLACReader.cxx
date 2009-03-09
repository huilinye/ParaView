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

#include "vtkPSLACReader.h"

#include "vtkCellArray.h"
#include "vtkCompositeDataIterator.h"
#include "vtkDummyController.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkMultiProcessController.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSortDataArray.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkUnstructuredGrid.h"
#include "vtkDoubleArray.h"

#include "vtkSmartPointer.h"
#define VTK_CREATE(type, name) \
  vtkSmartPointer<type> name = vtkSmartPointer<type>::New()

#include <netcdf.h>

//=============================================================================
#define MY_MIN(x, y)    ((x) < (y) ? (x) : (y))
#define MY_MAX(x, y)    ((x) < (y) ? (y) : (x))

//=============================================================================
#define CALL_NETCDF(call)                       \
  { \
    int errorcode = call; \
    if (errorcode != NC_NOERR) \
      { \
      vtkErrorMacro(<< "netCDF Error: " << nc_strerror(errorcode)); \
      return 0; \
      } \
  }

#define WRAP_NETCDF(call) \
  { \
    int errorcode = call; \
    if (errorcode != NC_NOERR) return errorcode; \
  }

#ifdef VTK_USE_64BIT_IDS
#ifdef NC_INT64
// This may or may not work with the netCDF 4 library reading in netCDF 3 files.
#define nc_get_vars_vtkIdType nc_get_vars_longlong
#else // NC_INT64
static int nc_get_vars_vtkIdType(int ncid, int varid,
                                 const size_t start[], const size_t count[],
                                 const ptrdiff_t stride[],
                                 vtkIdType *ip)
{
  // Step 1, figure out how many entries in the given variable.
  int numdims;
  WRAP_NETCDF(nc_inq_varndims(ncid, varid, &numdims));
  vtkIdType numValues = 1;
  for (int dim = 0; dim < numdims; dim++)
    {
    numValues *= count[dim];
    }

  // Step 2, read the data in as 32 bit integers.  Recast the input buffer
  // so we do not have to create a new one.
  long *smallIp = reinterpret_cast<long*>(ip);
  WRAP_NETCDF(nc_get_vars_long(ncid, varid, start, count, stride, smallIp));

  // Step 3, recast the data from 32 bit integers to 64 bit integers.  Since we
  // are storing both in the same buffer, we need to be careful to not overwrite
  // uncopied 32 bit numbers with 64 bit numbers.  We can do that by copying
  // backwards.
  for (vtkIdType i = numValues-1; i >= 0; i--)
    {
    ip[i] = static_cast<vtkIdType>(smallIp[i]);
    }

  return NC_NOERR;
}
#endif // NC_INT64
#else // VTK_USE_64_BIT_IDS
#define nc_get_vars_vtkIdType nc_get_vars_int
#endif // VTK_USE_64BIT_IDS

//=============================================================================
static int NetCDFTypeToVTKType(nc_type type)
{
  switch (type)
    {
    case NC_BYTE: return VTK_UNSIGNED_CHAR;
    case NC_CHAR: return VTK_CHAR;
    case NC_SHORT: return VTK_SHORT;
    case NC_INT: return VTK_INT;
    case NC_FLOAT: return VTK_FLOAT;
    case NC_DOUBLE: return VTK_DOUBLE;
    default:
      vtkGenericWarningMacro(<< "Unknown netCDF variable type "
                             << type);
      return -1;
    }
}

//=============================================================================
// In this version, indexMap points from outArray to inArray.  All the values
// of outArray get filled.
template<class T>
void vtkPSLACReaderMapValues1(const T *inArray, T *outArray, int numComponents,
                              vtkIdTypeArray *indexMap, vtkIdType offset=0)
{
  vtkIdType numVals = indexMap->GetNumberOfTuples();
  for (vtkIdType i = 0; i < numVals; i++)
    {
    vtkIdType j = indexMap->GetValue(i) - offset;
    for (int c = 0; c < numComponents; c++)
      {
      outArray[numComponents*i+c] = inArray[numComponents*j+c];
      }
    }
}

// // In this version, indexMap points from inArray to outArray.  All the values
// // of inArray get copied.
// template<class T>
// void vtkPSLACReaderMapValues2(const T *inArray, T *outArray, int numComponents,
//                               vtkIdTypeArray *indexMap)
// {
//   vtkIdType numVals = indexMap->GetNumberOfTuples();
//   for (vtkIdType i = 0; i < numVals; i++)
//     {
//     vtkIdType j = indexMap->GetValue(i);
//     for (int c = 0; c < numComponents; c++)
//       {
//       outArray[numComponents*j+c] = inArray[numComponents*i+c];
//       }
//     }
// }

//=============================================================================
vtkCxxRevisionMacro(vtkPSLACReader, "$Revision$");
vtkStandardNewMacro(vtkPSLACReader);

vtkCxxSetObjectMacro(vtkPSLACReader, Controller, vtkMultiProcessController);

//-----------------------------------------------------------------------------
vtkPSLACReader::vtkPSLACReader()
{
  this->Controller = NULL;
  this->SetController(vtkMultiProcessController::GetGlobalController());
  if (!this->Controller)
    {
    this->SetController(vtkSmartPointer<vtkDummyController>::New());
    }
}

vtkPSLACReader::~vtkPSLACReader()
{
  this->SetController(NULL);
}

void vtkPSLACReader::PrintSelf(ostream &os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Controller: " << this->Controller << endl;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::RequestInformation(vtkInformation *request,
                                       vtkInformationVector **inputVector,
                                       vtkInformationVector *outputVector)
{
  // It would be more efficient to read the meta data on just process 0 and
  // propgate to the rest.  However, this will probably have a profound effect
  // only on big jobs accessing parallel file systems.  Untill we need that,
  // I'm not going to bother.
  if (!this->Superclass::RequestInformation(request, inputVector, outputVector))
    {
    return 0;
    }

  if (!this->Controller)
    {
    vtkErrorMacro(<< "I need a Controller to read the data.");
    return 0;
    }

  // We only work if each process requests the piece corresponding to its
  // own local process id.  Hint at this by saying that we support the same
  // amount of pieces as processes.
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  outInfo->Set(vtkStreamingDemandDrivenPipeline::MAXIMUM_NUMBER_OF_PIECES(),
               this->Controller->GetNumberOfProcesses());

  return 1;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::RequestData(vtkInformation *request,
                                vtkInformationVector **inputVector,
                                vtkInformationVector *outputVector)
{
  // Check to make sure the pieces match the processes.
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  this->RequestedPiece
    = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_PIECE_NUMBER());
  this->NumberOfPieces
    = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_NUMBER_OF_PIECES());
  if (   (this->RequestedPiece != this->Controller->GetLocalProcessId())
      || (this->NumberOfPieces != this->Controller->GetNumberOfProcesses()) )
    {
    vtkErrorMacro(<< "Process numbers do not match piece numbers.");
    return 0;
    }

  // RequestData will call other methods that we have overloaded to read
  // partitioned pieces.
  int retval =this->Superclass::RequestData(request, inputVector, outputVector);

  // Clean up search structure.  When we support time data, we may want to keep
  // this arround so that we do not have to recalculate them for every time
  // step.
  this->LocalToGlobalIds = NULL;
  this->PointsExpectedFromProcessesLengths = NULL;
  this->PointsExpectedFromProcessesOffsets = NULL;
  this->PointsToSendToProcesses = NULL;
  this->PointsToSendToProcessesLengths = NULL;
  this->PointsToSendToProcessesOffsets = NULL;

  return retval;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadTetrahedronInteriorArray(int meshFD,
                                                 vtkIdTypeArray *connectivity)
{
  int tetInteriorVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "tetrahedron_interior", &tetInteriorVarId));
  vtkIdType numTets
    = this->GetNumTuplesInVariable(meshFD, tetInteriorVarId, NumPerTetInt);

  vtkIdType numTetsPerPiece = numTets/this->NumberOfPieces + 1;
  vtkIdType startTet = this->RequestedPiece*numTetsPerPiece;
  vtkIdType endTet = startTet + numTetsPerPiece;
  if (endTet > numTets) endTet = numTets;

  size_t start[2];
  size_t count[2];

  start[0] = startTet;  count[0] = endTet - startTet;
  start[1] = 0;         count[1] = NumPerTetInt;

  connectivity->Initialize();
  connectivity->SetNumberOfComponents(count[1]);
  connectivity->SetNumberOfTuples(count[0]);
  CALL_NETCDF(nc_get_vars_vtkIdType(meshFD, tetInteriorVarId,
                                    start, count, NULL,
                                    connectivity->GetPointer(0)));

  return 1;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadTetrahedronExteriorArray(int meshFD,
                                                 vtkIdTypeArray *connectivity)
{
  int tetExteriorVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "tetrahedron_exterior", &tetExteriorVarId));
  vtkIdType numTets
    = this->GetNumTuplesInVariable(meshFD, tetExteriorVarId, NumPerTetExt);

  vtkIdType numTetsPerPiece = numTets/this->NumberOfPieces + 1;
  vtkIdType startTet = this->RequestedPiece*numTetsPerPiece;
  vtkIdType endTet = startTet + numTetsPerPiece;
  if (endTet > numTets) endTet = numTets;

  size_t start[2];
  size_t count[2];

  start[0] = startTet;  count[0] = endTet - startTet;
  start[1] = 0;         count[1] = NumPerTetExt;

  connectivity->Initialize();
  connectivity->SetNumberOfComponents(count[1]);
  connectivity->SetNumberOfTuples(count[0]);
  CALL_NETCDF(nc_get_vars_vtkIdType(meshFD, tetExteriorVarId,
                                    start, count, NULL,
                                    connectivity->GetPointer(0)));

  return 1;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadConnectivity(int meshFD, vtkMultiBlockDataSet *output)
{
  //---------------------------------
  // Call the superclass to read the arrays from disk and assemble the
  // primitives.  The superclass will call the ReadTetrahedron*Array methods,
  // which we have overridden to read only a partition of the cells.
  if (!this->Superclass::ReadConnectivity(meshFD, output)) return 0;

  // ---------------------------------
  // All the cells have "global" ids.  That is, an index into a global list of
  // all possible points.  We don't want to have to read in all points in all
  // processes, so here we are going to figure out what points we need to load
  // locally, make maps between local and global ids, and convert the ids in the
  // connectivity arrays from global ids to local ids.

  this->LocalToGlobalIds = vtkSmartPointer<vtkIdTypeArray>::New();
  this->LocalToGlobalIds->SetName("GlobalIds");

  vtkstd::vector< vtkstd::pair<vtkIdType, vtkIdType> > edgesNeeded;

  // Iterate over all points of all cells and mark what points we encounter
  // in GlobalToLocalIds.
  this->GlobalToLocalIds.clear();
  VTK_CREATE(vtkCompositeDataIterator, outputIter);
  for (outputIter.TakeReference(output->NewIterator());
       !outputIter->IsDoneWithTraversal(); outputIter->GoToNextItem())
    {
    vtkUnstructuredGrid *ugrid
      = vtkUnstructuredGrid::SafeDownCast(output->GetDataSet(outputIter));
    vtkCellArray *cells = ugrid->GetCells();

    vtkIdType npts, *pts;
    for (cells->InitTraversal(); cells->GetNextCell(npts, pts); )
      {
      for (vtkIdType i = 0; i < npts; i++)
        {
        // The following inserts an entry into the map if one does not exist.
        // We will assign actual local ids later.
        this->GlobalToLocalIds[pts[i]] = -1;
        }
      if (output->GetMetaData(outputIter)->Get(IS_EXTERNAL_SURFACE()))
        {
        edgesNeeded.push_back (vtkstd::make_pair(MY_MIN(pts[0], pts[1]), 
                                                 MY_MAX(pts[0], pts[1])));
        edgesNeeded.push_back (vtkstd::make_pair(MY_MIN(pts[1], pts[2]), 
                                                 MY_MAX(pts[1], pts[2])));
        edgesNeeded.push_back (vtkstd::make_pair(MY_MIN(pts[2], pts[0]), 
                                                 MY_MAX(pts[2], pts[0])));
        }
      }
    }

  // ---------------------------------
  // Now that we know all the global ids we have, create a map from local
  // to global ids.  First we'll just copy the global ids into the array and
  // then sort them.  Sorting them will make the global ids monotonically
  // increasing, which means that when we get data from another process we
  // can just copy it into a block of memory.  We are only calculating the
  // local to global id map for now.  We will fill the global to local id
  // later when we iterate over the local ids.
  this->LocalToGlobalIds->Allocate(this->GlobalToLocalIds.size());
  for (GlobalToLocalIdType::iterator itr = this->GlobalToLocalIds.begin();
       itr != this->GlobalToLocalIds.end(); itr++)
    {
    this->LocalToGlobalIds->InsertNextValue(itr->first);
    }
  vtkSortDataArray::Sort(this->LocalToGlobalIds);

  // ---------------------------------
  // Now that we have the local to global id maps, we can determine which
  // process will send what point data where.  This is also where we assign
  // local ids to global ids (i.e. determine locally where we store each point).
  this->PointsExpectedFromProcessesLengths = vtkSmartPointer<vtkIdTypeArray>::New();
  this->PointsExpectedFromProcessesLengths->SetNumberOfTuples(this->NumberOfPieces);
  this->PointsExpectedFromProcessesOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
  this->PointsExpectedFromProcessesOffsets->SetNumberOfTuples(this->NumberOfPieces);
  this->PointsToSendToProcesses = vtkSmartPointer<vtkIdTypeArray>::New();
  this->PointsToSendToProcessesLengths = vtkSmartPointer<vtkIdTypeArray>::New();
  this->PointsToSendToProcessesLengths->SetNumberOfTuples(this->NumberOfPieces);
  this->PointsToSendToProcessesOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
  this->PointsToSendToProcessesOffsets->SetNumberOfTuples(this->NumberOfPieces);

  // Record how many global points there are.
  int coordsVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "coords", &coordsVarId));
  this->NumberOfGlobalPoints
    = this->GetNumTuplesInVariable(meshFD, coordsVarId, 3);

  // Iterate over our LocalToGlobalIds map and determine which process reads
  // which points.
  vtkIdType localId = 0;
  vtkIdType numLocalIds = this->LocalToGlobalIds->GetNumberOfTuples();
  for (int process = 0; process < this->NumberOfPieces; process++)
    {
    VTK_CREATE(vtkIdTypeArray, pointList);
    pointList->Allocate(this->NumberOfGlobalPoints/this->NumberOfPieces,
                        this->NumberOfGlobalPoints/this->NumberOfPieces);
    vtkIdType lastId = this->EndPointRead(process);
    for ( ; (localId < numLocalIds); localId++)
      {
      vtkIdType globalId = this->LocalToGlobalIds->GetValue(localId);
      if (globalId >= lastId) break;
      this->GlobalToLocalIds[globalId] = localId;
      pointList->InsertNextValue(globalId);
      }

    // pointList now has all the global ids for points that will be loaded by
    // process.  Send those ids to process so that it knows what data to send
    // back when reading in point data.
    vtkIdType numPoints = pointList->GetNumberOfTuples();
    this->PointsExpectedFromProcessesLengths->SetValue(process, numPoints);
    this->Controller->Gather(&numPoints,
                             this->PointsToSendToProcessesLengths->WritePointer(0,this->NumberOfPieces),
                             1, process);
    vtkIdType offset = 0;
    if (process == this->RequestedPiece)
      {
      for (int i = 0; i < this->NumberOfPieces; i++)
        {
        this->PointsToSendToProcessesOffsets->SetValue(i, offset);
        offset += this->PointsToSendToProcessesLengths->GetValue(i);
        }
      this->PointsToSendToProcesses->SetNumberOfTuples(offset);
      }
    this->Controller->GatherV(
                           pointList->GetPointer(0),
                           this->PointsToSendToProcesses->WritePointer(0,offset),
                           numPoints,
                           this->PointsToSendToProcessesLengths->GetPointer(0),
                           this->PointsToSendToProcessesOffsets->GetPointer(0),
                           process);
    }

  // Calculate the offsets for the incoming point data into the local array.
  vtkIdType offset = 0;
  for (int process = 0; process < this->NumberOfPieces; process++)
    {
    this->PointsExpectedFromProcessesOffsets->SetValue(process, offset);
    offset += this->PointsExpectedFromProcessesLengths->GetValue(process);
    }

  // Now that we have a complete map from global to local ids, modify the
  // connectivity arrays to use local ids instead of global ids.
  for (outputIter.TakeReference(output->NewIterator());
       !outputIter->IsDoneWithTraversal(); outputIter->GoToNextItem())
    {
    vtkUnstructuredGrid *ugrid
      = vtkUnstructuredGrid::SafeDownCast(output->GetDataSet(outputIter));
    vtkCellArray *cells = ugrid->GetCells();

    vtkIdType npts, *pts;
    for (cells->InitTraversal(); cells->GetNextCell(npts, pts); )
      {
      for (vtkIdType i = 0; i < npts; i++)
        {
        pts[i] = this->GlobalToLocalIds[pts[i]];
        }
      }
    }

  // Record the global ids in the point data.
  vtkPointData *pd = vtkPointData::SafeDownCast(
                    output->GetInformation()->Get(vtkSLACReader::POINT_DATA()));
  pd->SetGlobalIds(this->LocalToGlobalIds);
  pd->SetPedigreeIds(this->LocalToGlobalIds);

  if (this->ReadMidpoints)
    {
    // Setup the Edge transfers 
    this->EdgesExpectedFromProcessesLengths = vtkSmartPointer<vtkIdTypeArray>::New();
    this->EdgesExpectedFromProcessesLengths->SetNumberOfTuples(this->NumberOfPieces);
    this->EdgesExpectedFromProcessesOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
    this->EdgesExpectedFromProcessesOffsets->SetNumberOfTuples(this->NumberOfPieces);
    this->EdgesToSendToProcesses = vtkSmartPointer<vtkIdTypeArray>::New();
    this->EdgesToSendToProcessesLengths = vtkSmartPointer<vtkIdTypeArray>::New();
    this->EdgesToSendToProcessesLengths->SetNumberOfTuples(this->NumberOfPieces);
    this->EdgesToSendToProcessesOffsets = vtkSmartPointer<vtkIdTypeArray>::New();
    this->EdgesToSendToProcessesOffsets->SetNumberOfTuples(this->NumberOfPieces);

    vtkstd::vector< vtkSmartPointer<vtkIdTypeArray> > edgeLists (this->NumberOfPieces);
    for (int process = 0; process < this->NumberOfPieces; process ++)
      {
      edgeLists[process] = vtkSmartPointer<vtkIdTypeArray>::New ();
      edgeLists[process]->SetNumberOfComponents (2);
      }
    int pointsPerProcess = this->NumberOfGlobalPoints/this->NumberOfPieces + 1;
    for (size_t i = 0; i < edgesNeeded.size (); i ++)
      {
      int process = MY_MIN(edgesNeeded[i].first,edgesNeeded[i].second) / pointsPerProcess; 
      vtkIdType ids[2];
      ids[0] = edgesNeeded[i].first;
      ids[1] = edgesNeeded[i].second;
      edgeLists[process]->InsertNextTupleValue(static_cast<vtkIdType*>(ids));
      }
    for (int process = 0; process < this->NumberOfPieces; process ++)
      {
      vtkIdType numEdges = edgeLists[process]->GetNumberOfTuples();
      this->EdgesExpectedFromProcessesLengths->SetValue(process, numEdges);
      this->Controller->Gather(&numEdges,
                               this->EdgesToSendToProcessesLengths->WritePointer(0,this->NumberOfPieces),
                               1, process);
      offset = 0;
      if (process == this->RequestedPiece)
        {
        for (int i = 0; i < this->NumberOfPieces; i++)
          {
          this->EdgesToSendToProcessesOffsets->SetValue(i, offset);
          int len =  this->EdgesToSendToProcessesLengths->GetValue(i) * 2;
          this->EdgesToSendToProcessesLengths->SetValue (i, len);
          offset += len;
          }
        }
      this->EdgesToSendToProcesses->SetNumberOfComponents (2);
      this->EdgesToSendToProcesses->SetNumberOfTuples (offset/2);
      this->Controller->GatherV(edgeLists[process]->GetPointer(0),
                                this->EdgesToSendToProcesses->WritePointer(0,offset),
                                numEdges*2,
                                this->EdgesToSendToProcessesLengths->GetPointer(0),
                                this->EdgesToSendToProcessesOffsets->GetPointer(0),
                                process);
      }
    }
  return 1;
}

//-----------------------------------------------------------------------------
vtkSmartPointer<vtkDataArray> vtkPSLACReader::ReadPointDataArray(int ncFD,
                                                                 int varId)
{
  // Get the dimension info.  We should only need to worry about 1 or 2D arrays.
  int numDims;
  CALL_NETCDF(nc_inq_varndims(ncFD, varId, &numDims));
  if (numDims > 2)
    {
    vtkErrorMacro(<< "Sanity check failed.  "
                  << "Encountered array with too many dimensions.");
    return 0;
    }
  if (numDims < 1)
    {
    vtkErrorMacro(<< "Sanity check failed.  "
                  << "Encountered array with *no* dimensions.");
    return 0;
    }
  int dimIds[2];
  CALL_NETCDF(nc_inq_vardimid(ncFD, varId, dimIds));
  size_t numCoords;
  CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[0], &numCoords));
  if (numCoords != static_cast<size_t>(this->NumberOfGlobalPoints))
    {
    vtkErrorMacro(<< "Encountered inconsistent number of coordinates.");
    return 0;
    }
  size_t numComponents = 1;
  if (numDims > 1)
    {
    CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[1], &numComponents));
    }

  // Allocate an array of the right type.
  nc_type ncType;
  CALL_NETCDF(nc_inq_vartype(ncFD, varId, &ncType));
  int vtkType = NetCDFTypeToVTKType(ncType);
  if (vtkType < 1) return 0;
  vtkSmartPointer<vtkDataArray> dataArray;
  dataArray.TakeReference(vtkDataArray::CreateDataArray(vtkType));

  // Read the data from the file.
  size_t start[2], count[2];
  start[0] = this->StartPointRead(this->RequestedPiece);
  count[0] = this->EndPointRead(this->RequestedPiece) - start[0];
  start[1] = 0;  count[1] = numComponents;
  dataArray->SetNumberOfComponents(count[1]);
  dataArray->SetNumberOfTuples(count[0]);
  CALL_NETCDF(nc_get_vars(ncFD, varId, start, count, NULL,
                          dataArray->GetVoidPointer(0)));

  // We now need to redistribute the data.  Allocate an array to store the final
  // point data and a buffer to send data to the rest of the processes.
  vtkSmartPointer<vtkDataArray> finalDataArray;
  finalDataArray.TakeReference(vtkDataArray::CreateDataArray(vtkType));
  finalDataArray->SetNumberOfComponents(numComponents);
  finalDataArray->SetNumberOfTuples(this->LocalToGlobalIds->GetNumberOfTuples());

  vtkSmartPointer<vtkDataArray> sendBuffer;
  sendBuffer.TakeReference(vtkDataArray::CreateDataArray(vtkType));
  sendBuffer->SetNumberOfComponents(numComponents);
  sendBuffer->SetNumberOfTuples(
                            this->PointsToSendToProcesses->GetNumberOfTuples());
  switch (vtkType)
    {
    vtkTemplateMacro(vtkPSLACReaderMapValues1(
                                   (VTK_TT*)dataArray->GetVoidPointer(0),
                                   (VTK_TT*)sendBuffer->GetVoidPointer(0),
                                   numComponents,
                                   this->PointsToSendToProcesses,
                                   this->StartPointRead(this->RequestedPiece)));
    }

  // Scatter expects identifiers per value, not per tuple.  Thus, we (may)
  // need to adjust the lengths and offsets of what we send.
  VTK_CREATE(vtkIdTypeArray, sendLengths);
  sendLengths->SetNumberOfTuples(this->NumberOfPieces);
  VTK_CREATE(vtkIdTypeArray, sendOffsets);
  sendOffsets->SetNumberOfTuples(this->NumberOfPieces);
  for (int i = 0; i < this->NumberOfPieces; i++)
    {
    sendLengths->SetValue(i,
               this->PointsToSendToProcessesLengths->GetValue(i)*numComponents);
    sendOffsets->SetValue(i,
               this->PointsToSendToProcessesOffsets->GetValue(i)*numComponents);
    }

  // Let each process have a turn sending data to the other processes.
  // Upon receiving
  for (int proc = 0; proc < this->NumberOfPieces; proc++)
    {
    // Scatter data from source.  Note that lengths and offsets are only valid
    // on the source process.  All others are ignored.
    vtkIdType destLength
      = numComponents*this->PointsExpectedFromProcessesLengths->GetValue(proc);
    vtkIdType destOffset
      = numComponents*this->PointsExpectedFromProcessesOffsets->GetValue(proc);
    this->Controller->GetCommunicator()->ScatterVVoidArray(
                                     sendBuffer->GetVoidPointer(0),
                                     finalDataArray->GetVoidPointer(destOffset),
                                     sendLengths->GetPointer(0),
                                     sendOffsets->GetPointer(0),
                                     destLength, vtkType, proc);
    }

  return finalDataArray;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadCoordinates(int meshFD, vtkMultiBlockDataSet *output)
{
  // The superclass reads everything correctly because it will call our
  // ReadPointDataArray method, which will properly redistribute points.
  return this->Superclass::ReadCoordinates(meshFD, output);
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadFieldData(int modeFD, vtkMultiBlockDataSet *output)
{
  // The superclass reads everything correctly because it will call our
  // ReadPointDataArray method, which will properly redistribute points.
  return this->Superclass::ReadFieldData(modeFD, output);
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadMidpointCoordinates (
                                       int meshFD, 
                                       vtkMultiBlockDataSet *vtkNotUsed(output),
                                       vtkMidpointCoordinateMap &map)
{
  // Get the number of midpoints.
  int midpointsVar;
  CALL_NETCDF(nc_inq_varid(meshFD, "surface_midpoint", &midpointsVar));
  this->NumberOfGlobalMidpoints = this->GetNumTuplesInVariable(meshFD,midpointsVar,5);
  if (this->NumberOfGlobalMidpoints < 1) return 0;

  vtkIdType numMidpointsPerPiece = this->NumberOfGlobalMidpoints/this->NumberOfPieces + 1;
  vtkIdType startMidpoint = this->RequestedPiece*numMidpointsPerPiece;
  vtkIdType endMidpoint = startMidpoint + numMidpointsPerPiece;
  if (endMidpoint > this->NumberOfGlobalMidpoints) endMidpoint = this->NumberOfGlobalMidpoints;

  size_t starts[2];
  size_t counts[2];

  starts[0] = startMidpoint;  counts[0] = endMidpoint - startMidpoint;
  starts[1] = 0;              counts[1] = 5;

  VTK_CREATE (vtkDoubleArray, midpointData);
  midpointData->SetNumberOfComponents (counts[1]);
  midpointData->SetNumberOfTuples (counts[0]);
  CALL_NETCDF(nc_get_vars_double(meshFD, midpointsVar,
                                    starts, counts, NULL,
                                    midpointData->GetPointer(0)));
  
  // Collect the midpoints we've read on the processes that originally read the corresponding 
  // main points (the edge the midpoint is on).  These original processes are aware of who 
  // requested hose original points.  Thus they can redistribute the midpoints that correspond
  // to those processes that requested the original points.
  vtkstd::vector< vtkSmartPointer<vtkDoubleArray> > midpointsToDistribute (this->NumberOfPieces);
  for (int i = 0; i < this->NumberOfPieces; i ++) 
    {
    midpointsToDistribute[i] = vtkSmartPointer<vtkDoubleArray>::New ();
    midpointsToDistribute[i]->SetNumberOfComponents (6);
    }
  VTK_CREATE (vtkIdTypeArray, midpointsToDistributeLengths);
  midpointsToDistributeLengths->SetNumberOfTuples (this->NumberOfPieces);

  int pointsPerProcess = this->NumberOfGlobalPoints / this->NumberOfPieces + 1;
  for (vtkIdType i = 0; i < numMidpointsPerPiece; i ++) 
    {
    double *mp = midpointData->GetPointer (i*5);

    // find the processor the minimum edge point belongs to (by global id)
    vtkIdType process = static_cast<vtkIdType>(MY_MIN(mp[0], mp[1])) / pointsPerProcess;

    // insert the midpoint's global point id into the data
    double insert[6];
    memcpy (insert, mp, sizeof (double) * 5);
    insert[5] = i + startMidpoint + this->NumberOfGlobalPoints;

    midpointsToDistribute[process]->InsertNextTupleValue (insert);
    }

  for (vtkIdType process = 0; process < this->NumberOfPieces; process ++) 
    {
    midpointsToDistributeLengths->SetValue (process, 
                    midpointsToDistribute[process]->GetNumberOfTuples ()*6);
    }

  VTK_CREATE (vtkDoubleArray, MidpointsToRedistribute);
  MidpointsToRedistribute->SetNumberOfComponents (6);
  VTK_CREATE (vtkIdTypeArray, MidpointsToRedistributeLengths);
  MidpointsToRedistributeLengths->SetNumberOfTuples (this->NumberOfPieces);
  VTK_CREATE (vtkIdTypeArray, MidpointsToRedistributeOffsets);
  MidpointsToRedistributeOffsets->SetNumberOfTuples (this->NumberOfPieces);
  // collect all the midpoints with edge points globalID to corresponding process
  for (int process = 0; process < this->NumberOfPieces; process ++)
    {
    this->Controller->Gather (
                    midpointsToDistributeLengths->GetPointer(process),
                    MidpointsToRedistributeLengths->WritePointer(0, this->NumberOfPieces),
                    1, process);
    vtkIdType offset = 0;
    if (this->RequestedPiece == process)
      {
      for (int i = 0; i < this->NumberOfPieces; i ++)
        {
        MidpointsToRedistributeOffsets->SetValue (i, offset);
        offset += MidpointsToRedistributeLengths->GetValue (i);
        }
      MidpointsToRedistribute->SetNumberOfTuples (offset); 
      }
    this->Controller->GatherV (
                    midpointsToDistribute[process]->GetPointer (0),
                    MidpointsToRedistribute->WritePointer (0, offset),
                    midpointsToDistributeLengths->GetValue(process),
                    MidpointsToRedistributeLengths->GetPointer (0),
                    MidpointsToRedistributeOffsets->GetPointer (0),
                    process);
    }

  typedef vtksys::hash_map<vtkstd::pair<vtkIdType, vtkIdType>, double *,
                           vtkSLACReaderIdTypePairHash> MidpointsAvailableType;
  MidpointsAvailableType MidpointsAvailable;
  for (int i = 0; i < MidpointsToRedistribute->GetNumberOfTuples (); i ++) 
    {
    double *mp  = MidpointsToRedistribute->GetPointer (i*6);
    MidpointsAvailable.insert(
                      vtkstd::make_pair(vtkstd::make_pair(MY_MIN(mp[0], mp[1]), 
                                                          MY_MAX(mp[0], mp[1])),
                                        mp));
    }

  VTK_CREATE (vtkDoubleArray, MidpointsToReceive);
  MidpointsToReceive->SetNumberOfComponents (6);
  vtkIdType offset = 0;
  for (int process = 0; process < this->NumberOfPieces; process ++)
    {
    this->EdgesExpectedFromProcessesOffsets->SetValue (process, offset);
    vtkIdType len = this->EdgesExpectedFromProcessesLengths->GetValue (process) * 6;
    this->EdgesExpectedFromProcessesLengths->SetValue (process, len);
    offset += len;
    } 
  MidpointsToReceive->SetNumberOfTuples (offset/6);

  // redistribute midpoints based on the previous requests for edge points
  for (int process = 0; process < this->NumberOfPieces; process ++)
    {
    vtkIdType start = this->EdgesToSendToProcessesOffsets->GetValue(process);
    vtkIdType end = start + this->EdgesToSendToProcessesLengths->GetValue(process);

    start /= this->EdgesToSendToProcesses->GetNumberOfComponents ();
    end /= this->EdgesToSendToProcesses->GetNumberOfComponents ();
    VTK_CREATE (vtkDoubleArray, midpointsToRedistribute);
    midpointsToRedistribute->SetNumberOfComponents (6);
    for (vtkIdType i = start; i < end; i ++)
      {
      MidpointsAvailableType::const_iterator iter;
      vtkIdType e[2];
      this->EdgesToSendToProcesses->GetTupleValue (i, e);
      iter = MidpointsAvailable.find (vtkstd::make_pair(MY_MIN(e[0], e[1]), MY_MAX(e[0], e[1])));
      if (iter != MidpointsAvailable.end ())
        {
        midpointsToRedistribute->InsertNextTupleValue (iter->second);
        }
      else // in order to have the proper length we must insert empty.
        {
        double mp[6] = { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };
        midpointsToRedistribute->InsertNextTupleValue (mp);
        }
      }
    this->Controller->GatherV (
                    midpointsToRedistribute->GetPointer (0),
                    MidpointsToReceive->WritePointer (0, offset),
                    midpointsToRedistribute->GetNumberOfTuples () * 6,
                    this->EdgesExpectedFromProcessesLengths->GetPointer (0),
                    this->EdgesExpectedFromProcessesOffsets->GetPointer (0),
                    process);
    }
  
  // finally, we have all midpoints that correspond to edges we know about
  // convert their edge points to localId and insert into the map and return.
  vtkIdType numMids = MidpointsToReceive->GetNumberOfTuples ();
  typedef vtksys::hash_map<vtkIdType, vtkIdType, vtkSLACReaderIdTypeHash> localMapType;
  localMapType localMap;
  for (vtkIdType i = 0; i < numMids; i ++)
    {
    double *mp = MidpointsToReceive->GetPointer (i * 6);
    if (mp[0] < 0) continue;
     
    vtkIdType local0 = this->GlobalToLocalIds[static_cast<vtkIdType>(mp[0])];
    vtkIdType local1 = this->GlobalToLocalIds[static_cast<vtkIdType>(mp[1])];
    localMapType::const_iterator iter;
    iter = localMap.find (static_cast<vtkIdType>(mp[5]));
    vtkIdType index;
    if (iter == localMap.end ())
      {
      index = this->LocalToGlobalIds->InsertNextTupleValue (
                      reinterpret_cast<vtkIdType*>(mp+5));
      localMap[mp[5]] = index;
      }
    else
      {
      index = iter->second;
      }
    map.insert(vtkstd::make_pair(vtkstd::make_pair(local0, local1),
                                 vtkSLACReader::vtkMidpoint(mp+2, index)));
    }
  return 1;
}

//-----------------------------------------------------------------------------
int vtkPSLACReader::ReadMidpointData(int meshFD, vtkMultiBlockDataSet *output)
{
  int result = this->Superclass::ReadMidpointData(meshFD, output); 
  if (result != 1)
    {
    return result;
    }
  // add global IDs for midpoints added that weren't in the file
  vtkPoints *points = vtkPoints::SafeDownCast(
                        output->GetInformation()->Get(vtkSLACReader::POINTS()));
  vtkIdType pointsAdded = points->GetNumberOfPoints () - 
          this->LocalToGlobalIds->GetNumberOfTuples (); 
  // Use the maximum number of points added so that the offsets don't overlap
  // There will be gaps and shared edges between two processes will get different ids
  // TODO: Will this cause problems?
  vtkIdType maxPointsAdded;
  this->Controller->AllReduce (&pointsAdded, &maxPointsAdded, 1, vtkCommunicator::MAX_OP);

  vtkIdType start = this->NumberOfGlobalPoints + this->NumberOfGlobalMidpoints + 
          this->RequestedPiece*maxPointsAdded;
  vtkIdType end = start + pointsAdded;
  for (vtkIdType i = start; i < end; i ++) 
    {
    this->LocalToGlobalIds->InsertNextTupleValue (&i);
    }

  return 1;
}
