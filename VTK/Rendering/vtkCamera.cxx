/*=========================================================================

  Program:   OSCAR 
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

Description:
---------------------------------------------------------------------------
This file is part of the vis library

- Ken Martin

=========================================================================*/
#include "Camera.hh"

vlCamera::vlCamera()
{
  this->FocalPoint[0] = 0.0;
  this->FocalPoint[1] = 0.0;
  this->FocalPoint[2] = 0.0;

  this->Position[0] = 0.0;
  this->Position[1] = 0.0;
  this->Position[2] = 1.0;

  this->ViewUp[0] = 0.0;
  this->ViewUp[1] = 1.0;
  this->ViewUp[2] = 0.0;

  this->ViewAngle = 30.0;

  this->ClippingRange[0] = 0.01;
  this->ClippingRange[1] = 1000.01;
}

void vlCamera::SetPosition(float X, float Y, float Z)
{
  this->Position[0] = X;
  this->Position[1] = Y;
  this->Position[2] = Z;
}
void vlCamera::SetPosition(float a[3])
{
  this->Position[0] = a[0];
  this->Position[1] = a[1];
  this->Position[2] = a[2];
}
float *vlCamera::GetPosition()
{
  return (this->Position);
}

void vlCamera::SetFocalPoint(float X, float Y, float Z)
{
  this->FocalPoint[0] = X; 
  this->FocalPoint[1] = Y; 
  this->FocalPoint[2] = Z;
}
void vlCamera::SetFocalPoint(float a[3])
{
  this->FocalPoint[0] = a[0];
  this->FocalPoint[1] = a[1];
  this->FocalPoint[2] = a[2];
}
float *vlCamera::GetFocalPoint()
{
  return (this->FocalPoint);
}

void vlCamera::SetViewUp(float X, float Y, float Z)
{
  this->ViewUp[0] = X;
  this->ViewUp[1] = Y;
  this->ViewUp[2] = Z;
}
void vlCamera::SetViewUp(float a[3])
{
  this->ViewUp[0] = a[0];
  this->ViewUp[1] = a[1];
  this->ViewUp[2] = a[2];
}
float *vlCamera::GetViewUp()
{
  return (this->ViewUp);
}
