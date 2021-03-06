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
#include "vtkExtractSelectedBlock.h"

#include "vtkCompositeDataIterator.h"
#include "vtkUnsignedIntArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"

#include <vtkstd/set>
vtkStandardNewMacro(vtkExtractSelectedBlock);
vtkCxxRevisionMacro(vtkExtractSelectedBlock, "$Revision$");
//----------------------------------------------------------------------------
vtkExtractSelectedBlock::vtkExtractSelectedBlock()
{
}

//----------------------------------------------------------------------------
vtkExtractSelectedBlock::~vtkExtractSelectedBlock()
{
}

//----------------------------------------------------------------------------
int vtkExtractSelectedBlock::FillInputPortInformation(
  int port, vtkInformation* info)
{
  this->Superclass::FillInputPortInformation(port, info);

  // now add our info
  if (port == 0)
    {
    // Can work with composite datasets.
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataObject");
    }

  return 1;
}


//----------------------------------------------------------------------------
// Needed because parent class sets output type to input type
// and we sometimes want to change it to make an UnstructuredGrid regardless of
// input type
int vtkExtractSelectedBlock::RequestDataObject(
  vtkInformation* req,
  vtkInformationVector** inputVector ,
  vtkInformationVector* outputVector)
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  if (!inInfo)
    {
    return 0;
    }

  vtkCompositeDataSet *input = vtkCompositeDataSet::GetData(inInfo);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  if (input)
    {
    vtkMultiBlockDataSet* output = vtkMultiBlockDataSet::GetData(outInfo);
    if (!output)
      {
      output = vtkMultiBlockDataSet::New();
      output->SetPipelineInformation(outInfo);
      output->Delete();
      }
    return 1;
    }

  return this->Superclass::RequestDataObject(req, inputVector, outputVector);
}


//----------------------------------------------------------------------------
int vtkExtractSelectedBlock::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  // get the info objects
  vtkInformation *selInfo = inputVector[1]->GetInformationObject(0);
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation *outInfo = outputVector->GetInformationObject(0);

  vtkCompositeDataSet* cd = vtkCompositeDataSet::GetData(inInfo);
  if (!cd)
    {
    vtkDataObject* outputDO = vtkDataObject::GetData(outInfo);
    outputDO->ShallowCopy(vtkDataObject::GetData(inInfo));
    return 1;
    }

  if (!selInfo)
    {
    //When not given a selection, quietly select nothing.
    return 1;
    }

  vtkSelection* input = vtkSelection::GetData(selInfo);
  vtkSelectionNode* node = input->GetNode(0);
  if (input->GetNumberOfNodes() != 1 || node->GetContentType() != vtkSelectionNode::BLOCKS)
    {
    vtkErrorMacro("This filter expects a single-node selection of type BLOCKS.");
    return 0;
    }
  vtkMultiBlockDataSet* output = vtkMultiBlockDataSet::GetData(outInfo);

  bool inverse = (node->GetProperties()->Has(vtkSelectionNode::INVERSE()) &&
    node->GetProperties()->Get(vtkSelectionNode::INVERSE()) == 1);

  output->CopyStructure(cd);
  vtkUnsignedIntArray* selectionList = vtkUnsignedIntArray::SafeDownCast(
    node->GetSelectionList());
  if (selectionList)
    {
    vtkstd::set<unsigned int> blocks;
    vtkIdType numValues = selectionList->GetNumberOfTuples();
    for (vtkIdType cc=0; cc < numValues; cc++)
      {
      blocks.insert(selectionList->GetValue(cc));
      }

    if (numValues > 0)
      {
      vtkCompositeDataIterator* citer = cd->NewIterator();
      for (citer->InitTraversal(); !citer->IsDoneWithTraversal(); 
        citer->GoToNextItem())
        {
        vtkstd::set<unsigned int>::iterator fiter = 
          blocks.find(citer->GetCurrentFlatIndex());
        if ((inverse && fiter == blocks.end()) || (!inverse && fiter != blocks.end()))
          {
          output->SetDataSet(citer, citer->GetCurrentDataObject());
          }
        }
      citer->Delete();
      }
    }
  return 1;
}

//----------------------------------------------------------------------------
void vtkExtractSelectedBlock::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

