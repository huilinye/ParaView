/*=========================================================================

   Program: ParaView
   Module:    $RCSfile$

   Copyright (c) 2009 Michael Wild, Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

========================================================================*/
#ifndef __pqFilesOpenEvent_h
#define __pqFilesOpenEvent_h

#include <QEvent>
#include <QStringList>
#include "pqComponentsExport.h"

/// Custom event class to handle Apple OpenDocuments event
class PQCOMPONENTS_EXPORT pqFilesOpenEvent : public QEvent
{
  typedef QEvent Superclass;
public:
  /// The unique event ID
  static const int eventId;

  /// Construct an event with a given list of files
  pqFilesOpenEvent(const QStringList& files);

  /// Returns the list of files to open
  const QStringList& files() const { return fileNames; }

private:
  const QStringList fileNames;
};

#endif


