/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Thanks:    Thanks to C. Charles Law who developed this class.

Copyright (c) 1993-1995 Ken Martin, Will Schroeder, Bill Lorensen.

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
#include <math.h>

#include "vtkImageGradientMagnitude.h"


//----------------------------------------------------------------------------
// Construct an instance of vtkImageGradientMagnitude fitler.
vtkImageGradientMagnitude::vtkImageGradientMagnitude()
{
  this->Dimensionality = 2;
  this->HandleBoundaries = 1;
}


//----------------------------------------------------------------------------
void vtkImageGradientMagnitude::PrintSelf(ostream& os, vtkIndent indent)
{
  this->vtkImageToImageFilter::PrintSelf(os, indent);
  os << indent << "HandleBoundaries: " << this->HandleBoundaries << "\n";
  os << indent << "Dimensionality: " << this->Dimensionality << "\n";
}

//----------------------------------------------------------------------------
// This method is passed a region that holds the image extent of this filters
// input, and changes the region to hold the image extent of this filters
// output.
void vtkImageGradientMagnitude::ExecuteInformation()
{  
  int extent[6];
  int idx;

  this->GetInput()->GetWholeExtent(extent);
  if ( ! this->HandleBoundaries)
    {
    // shrink output image extent.
    for (idx = 0; idx < this->Dimensionality; ++idx)
      {
      extent[idx*2] += 1;
      extent[idx*2 + 1] -= 1;
      }
    }
  
  this->GetOutput()->SetWholeExtent(extent);
}


//----------------------------------------------------------------------------
// This method computes the input extent necessary to generate the output.
void vtkImageGradientMagnitude::ComputeInputUpdateExtent(int inExt[6],
								 int outExt[6])
{
  int *wholeExtent;
  int idx;

  wholeExtent = this->GetInput()->GetWholeExtent();
  
  memcpy(inExt,outExt,6*sizeof(int));
  
  // grow input whole extent.
  for (idx = 0; idx < this->Dimensionality; ++idx)
    {
    inExt[idx*2] -= 1;
    inExt[idx*2+1] += 1;
    if (this->HandleBoundaries)
      {
      // we must clip extent with whole extent is we hanlde boundaries.
      if (inExt[idx*2] < wholeExtent[idx*2])
	{
	inExt[idx*2] = wholeExtent[idx*2];
	}
      if (inExt[idx*2 + 1] > wholeExtent[idx*2 + 1])
	{
	inExt[idx*2 + 1] = wholeExtent[idx*2 + 1];
	}
      }
    }
}





//----------------------------------------------------------------------------
// This execute method handles boundaries.
// it handles boundaries. Pixels are just replicated to get values 
// out of extent.
template <class T>
static void vtkImageGradientMagnitudeExecute(vtkImageGradientMagnitude *self,
					     vtkImageData *inData, T *inPtr,
					     vtkImageData *outData, T *outPtr,
					     int outExt[6], int id)
{
  int idxC, idxX, idxY, idxZ;
  int maxC, maxX, maxY, maxZ;
  int inIncX, inIncY, inIncZ;
  int outIncX, outIncY, outIncZ;
  unsigned long count = 0;
  unsigned long target;
  int axesNum;
  int *wholeExtent, *inIncs;
  float r[3], d, sum;
  int useZMin, useZMax, useYMin, useYMax, useXMin, useXMax;
  
  // find the region to loop over
  maxC = outData->GetNumberOfScalarComponents();
  maxX = outExt[1] - outExt[0];
  maxY = outExt[3] - outExt[2]; 
  maxZ = outExt[5] - outExt[4];
  target = (unsigned long)((maxZ+1)*(maxY+1)/50.0);
  target++;

  // Get the dimensionality of the gradient.
  axesNum = self->GetDimensionality();
  
  // Get increments to march through data 
  inData->GetContinuousIncrements(outExt, inIncX, inIncY, inIncZ);
  outData->GetContinuousIncrements(outExt, outIncX, outIncY, outIncZ);

  // The data spacing is important for computing the gradient.
  inData->GetSpacing(r);
  r[0] = 0.5 / r[0];
  r[1] = 0.5 / r[1];
  r[2] = 0.5 / r[2];

  // get some other info we need
  inIncs = inData->GetIncrements(); 
  wholeExtent = inData->GetExtent(); 

  // Loop through ouput pixels
  for (idxZ = 0; idxZ <= maxZ; idxZ++)
    {
    useZMin = ((idxZ + outExt[4]) <= wholeExtent[4]) ? 0 : -inIncs[2];
    useZMax = ((idxZ + outExt[4]) >= wholeExtent[5]) ? 0 : inIncs[2];
    for (idxY = 0; !self->AbortExecute && idxY <= maxY; idxY++)
      {
      if (!id) 
	{
	if (!(count%target))
	  {
	  self->UpdateProgress(count/(50.0*target));
	  }
	count++;
	}
      useYMin = ((idxY + outExt[2]) <= wholeExtent[2]) ? 0 : -inIncs[1];
      useYMax = ((idxY + outExt[2]) >= wholeExtent[3]) ? 0 : inIncs[1];
      for (idxX = 0; idxX <= maxX; idxX++)
	{
	useXMin = ((idxX + outExt[0]) <= wholeExtent[0]) ? 0 : -inIncs[0];
	useXMax = ((idxX + outExt[0]) >= wholeExtent[1]) ? 0 : inIncs[0];
	for (idxC = 0; idxC < maxC; idxC++)
	  {
	  // do X axis
	  d = (float)(inPtr[useXMin]);
	  d -= (float)(inPtr[useXMax]);
	  d *= r[0]; // multiply by the data spacing
	  sum = d * d;
	  // do y axis
	  d = (float)(inPtr[useYMin]);
	  d -= (float)(inPtr[useYMax]);
	  d *= r[1]; // multiply by the data spacing
	  sum += (d * d);
	  if (axesNum == 3)
	    {
	    // do z axis
	    d = (float)(inPtr[useZMin]);
	    d -= (float)(inPtr[useZMax]);
	    d *= r[2]; // multiply by the data spacing
	    sum += (d * d);
	    }
	  *outPtr = (T)(sqrt(sum));
	  outPtr++;
	  inPtr++;
	  }
	}
      outPtr += outIncY;
      inPtr += inIncY;
      }
    outPtr += outIncZ;
    inPtr += inIncZ;
    }
}


//----------------------------------------------------------------------------
// This method contains a switch statement that calls the correct
// templated function for the input data type.  The output data
// must match input type.  This method does handle boundary conditions.
void vtkImageGradientMagnitude::ThreadedExecute(vtkImageData *inData, 
					 vtkImageData *outData,
					 int outExt[6], int id)
{
  void *inPtr = inData->GetScalarPointerForExtent(outExt);
  void *outPtr = outData->GetScalarPointerForExtent(outExt);
  
  vtkDebugMacro(<< "Execute: inData = " << inData 
		<< ", outData = " << outData);
  
  // this filter expects that input is the same type as output.
  if (inData->GetScalarType() != outData->GetScalarType())
    {
    vtkErrorMacro(<< "Execute: input ScalarType, " << inData->GetScalarType()
                  << ", must match out ScalarType " << outData->GetScalarType());
    return;
    }
  
  switch (inData->GetScalarType())
    {
    case VTK_DOUBLE:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (double *)(inPtr), 
			  outData, (double *)(outPtr), outExt, id);
      break;
    case VTK_FLOAT:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (float *)(inPtr), 
			  outData, (float *)(outPtr), outExt, id);
      break;
    case VTK_LONG:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (long *)(inPtr), 
			  outData, (long *)(outPtr), outExt, id);
      break;
    case VTK_UNSIGNED_LONG:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (unsigned long *)(inPtr), 
			  outData, (unsigned long *)(outPtr), outExt, id);
      break;
    case VTK_INT:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (int *)(inPtr), 
			  outData, (int *)(outPtr), outExt, id);
      break;
    case VTK_UNSIGNED_INT:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (unsigned int *)(inPtr), 
			  outData, (unsigned int *)(outPtr), outExt, id);
      break;
    case VTK_SHORT:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (short *)(inPtr), 
			  outData, (short *)(outPtr), outExt, id);
      break;
    case VTK_UNSIGNED_SHORT:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (unsigned short *)(inPtr), 
			  outData, (unsigned short *)(outPtr), outExt, id);
      break;
    case VTK_CHAR:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (char *)(inPtr), 
			  outData, (char *)(outPtr), outExt, id);
      break;
    case VTK_UNSIGNED_CHAR:
      vtkImageGradientMagnitudeExecute(this, 
			  inData, (unsigned char *)(inPtr), 
			  outData, (unsigned char *)(outPtr), outExt, id);
      break;
    default:
      vtkErrorMacro(<< "Execute: Unknown ScalarType");
      return;
    }
}






