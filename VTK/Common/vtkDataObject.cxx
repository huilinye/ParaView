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
#include "vtkDataObject.h"
#include "vtkDataInformation.h"
#include "vtkSource.h"
#include "vtkExtent.h"
#include "vtkDataInformation.h"
#include "vtkObjectFactory.h"



//----------------------------------------------------------------------------
vtkDataObject* vtkDataObject::New()
{
  // First try to create the object from the vtkObjectFactory
  vtkObject* ret = vtkObjectFactory::CreateInstance("vtkDataObject");
  if(ret)
    {
    return (vtkDataObject*)ret;
    }
  // If the factory was unable to create the object, then create it here.
  return new vtkDataObject;
}




// Initialize static member that controls global data release 
// after use by filter
static int vtkDataObjectGlobalReleaseDataFlag = 0;

//----------------------------------------------------------------------------
vtkDataObject::vtkDataObject()
{
  this->Source = NULL;
  // We have to assume that if a user is creating the data on their own,
  // then they will fill it with valid data.
  this->DataReleased = 0;
  this->ReleaseDataFlag = 0;
  this->FieldData = vtkFieldData::New();
  // --- streaming stuff ---
  this->WaitingForUpdate = 0;
  this->MemoryLimit = VTK_LARGE_INTEGER;
  // subclasses will delete these and set a more specific information object.
  this->Information = vtkDataInformation::New();
  this->UpdateExtent = vtkExtent::New();
}

//----------------------------------------------------------------------------
vtkDataObject::~vtkDataObject()
{
  this->FieldData->Delete();
  this->Information->Delete();
  this->Information = NULL;
  this->UpdateExtent->Delete();
  this->UpdateExtent = NULL;
}


//----------------------------------------------------------------------------
// Determine the modified time of this object
unsigned long int vtkDataObject::GetMTime()
{
  unsigned long result, t2;

  result = vtkObject::GetMTime();
  if ( this->FieldData )
    {
    unsigned long mtime = this->FieldData->GetMTime();
    result = ( mtime > result ? mtime : result);
    }
  
  t2 = this->Information->GetMTime();  
  if (t2 > result)
    {
    result = t2;
    }
  
  return result;
}

//----------------------------------------------------------------------------
void vtkDataObject::Initialize()
{
//
// We don't modify ourselves because the "ReleaseData" methods depend upon
// no modification when initialized.
//
  this->FieldData->Initialize();
}

//----------------------------------------------------------------------------
void vtkDataObject::SetGlobalReleaseDataFlag(int val)
{
  if (val == vtkDataObjectGlobalReleaseDataFlag)
    {
    return;
    }
  vtkDataObjectGlobalReleaseDataFlag = val;
}

//----------------------------------------------------------------------------
int vtkDataObject::GetGlobalReleaseDataFlag()
{
  return vtkDataObjectGlobalReleaseDataFlag;
}

//----------------------------------------------------------------------------
void vtkDataObject::ReleaseData()
{
  this->Initialize();
  this->DataReleased = 1;
}

//----------------------------------------------------------------------------
void vtkDataObject::DataHasBeenGenerated()
{
  this->DataReleased = 0;
  this->UpdateTime.Modified();
}

//----------------------------------------------------------------------------
int vtkDataObject::ShouldIReleaseData()
{
  if ( vtkDataObjectGlobalReleaseDataFlag || this->ReleaseDataFlag )
    {
    return 1;
    }
  else
    {
    return 0;
    }
}

//----------------------------------------------------------------------------
void vtkDataObject::Update()
{
  this->UpdateInformation();
  this->PreUpdate();
  this->InternalUpdate();
}

//----------------------------------------------------------------------------
void vtkDataObject::UpdateInformation()
{
  if (this->Source)
    {
    this->Source->UpdateInformation();
    }
}

//----------------------------------------------------------------------------
void vtkDataObject::PreUpdate() 
{
  // PreUpdate and Internal Update must occur in pairs, but PreUpdate can be called
  // many times before InternalUpdate.
  if (this->WaitingForUpdate)
    {
    return;
    }
  
  // Clip has to be before the Update check because:  If the update extent
  // after clipping is larger than current extent, then data is released ...
  // We might need another method here, but for now, this works.
  if ( ! this->ClipUpdateExtentWithWholeExtent())
    {
    // invalid update piece
    return;
    }

  // Do we need to update
  if (this->UpdateTime >= this->Information->GetPipelineMTime() 
      && ! this->DataReleased)
    {
    return;
    }
  
  this->WaitingForUpdate = 1;
  
  if (this->Source)
    {
    this->Source->PreUpdate(this);
    }
}


//----------------------------------------------------------------------------
// If there is no source, just assume user put data here.
void vtkDataObject::InternalUpdate()
{
  // PreUpdate did all the checks
  if (this->WaitingForUpdate == 0)
    {
    // We must not need to update.
    return;
    }
  
  if (this->Source)
    {
    this->Source->InternalUpdate(this);
    }
  
  this->WaitingForUpdate = 0;
}

//----------------------------------------------------------------------------
void vtkDataObject::CopyUpdateExtent(vtkDataObject *data)
{
  this->GetGenericUpdateExtent()->Copy(data->GetGenericUpdateExtent());
}

//----------------------------------------------------------------------------
void vtkDataObject::CopyInformation(vtkDataObject *data)
{
  this->GetDataInformation()->Copy(data->GetDataInformation());
}

//----------------------------------------------------------------------------
void vtkDataObject::PrintSelf(ostream& os, vtkIndent indent)
{
  vtkObject::PrintSelf(os,indent);

  if ( this->Source )
    {
    os << indent << "Source: " << this->Source << "\n";
    }
  else
    {
    os << indent << "Source: (none)\n";
    }

  os << indent << "Release Data: " << (this->ReleaseDataFlag ? "On\n" : "Off\n");
  os << indent << "Data Released: " << (this->DataReleased ? "True\n" : "False\n");
  
  os << indent << "Global Release Data: " 
     << (vtkDataObjectGlobalReleaseDataFlag ? "On\n" : "Off\n");

  os << indent << "UpdateTime: " << this->UpdateTime << endl;
  os << indent << "MemoryLimit: " << this->MemoryLimit << endl;
  os << indent << "Information:\n";
  this->Information->PrintSelf(os, indent.GetNextIndent());  
  
  if (this->UpdateExtent)
    {
    os << indent << "UpdateExtent: \n";
    this->UpdateExtent->PrintSelf(os, indent.GetNextIndent());
    }
  else
    {
    os << indent << "UpdateExtent: NULL\n";
    }
  
  os << indent << "Field Data:\n";
  this->FieldData->PrintSelf(os,indent.GetNextIndent());
}

void vtkDataObject::SetSource(vtkSource *arg)
{
  vtkDebugMacro(<< this->GetClassName() << " (" << this << "): setting Source to " << arg ); 
  if (this->Source != arg) 
    {
    vtkSource *tmp = this->Source;
    this->Source = arg; 
    if (this->Source != NULL) 
      { 
      this->Source->Register(this); 
      } 
    if (tmp != NULL) 
      { 
      tmp->UnRegister(this); 
      }
    this->Modified(); 
    } 
}


//----------------------------------------------------------------------------
void vtkDataObject::UnRegister(vtkObject *o)
{
  // detect the circular loop source <-> data
  // If we have two references and one of them is my data
  // and I am not being unregistered by my data, break the loop.
  if (this->ReferenceCount == 2 && this->Source != NULL &&
      o != this->Source && this->Source->InRegisterLoop(this))
    {
    this->SetSource(NULL);
    }
  
  this->vtkObject::UnRegister(o);
}

//----------------------------------------------------------------------------
unsigned long vtkDataObject::GetUpdateTime()
{
  return this->UpdateTime.GetMTime();
}



//----------------------------------------------------------------------------
void vtkDataObject::SetEstimatedWholeMemorySize(unsigned long v)
{
  this->Information->SetEstimatedWholeMemorySize(v);
}

//----------------------------------------------------------------------------
unsigned long vtkDataObject::GetEstimatedWholeMemorySize()  
{
  return this->Information->GetEstimatedWholeMemorySize();
}

//----------------------------------------------------------------------------
void vtkDataObject::SetPipelineMTime(unsigned long t) 
{
  this->Information->SetPipelineMTime(t);
}

//----------------------------------------------------------------------------
unsigned long vtkDataObject::GetPipelineMTime() 
{
  return this->Information->GetPipelineMTime();
}

//----------------------------------------------------------------------------
unsigned long vtkDataObject::GetActualMemorySize()
{
  return this->FieldData->GetActualMemorySize();
}
