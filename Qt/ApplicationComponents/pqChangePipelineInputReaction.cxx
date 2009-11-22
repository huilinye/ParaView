/*=========================================================================

   Program: ParaView
   Module:    $RCSfile$

   Copyright (c) 2005,2006 Sandia Corporation, Kitware Inc.
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
#include "pqChangePipelineInputReaction.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqFilterInputDialog.h"
#include "pqOutputPort.h"
#include "pqPipelineFilter.h"
#include "pqPipelineModel.h"
#include "pqServerManagerModel.h"
#include "pqServerManagerSelectionModel.h"
#include "pqUndoStack.h"
#include "vtkSMInputProperty.h"
#include "vtkSMProxy.h"

#include <QDebug>

//-----------------------------------------------------------------------------
pqChangePipelineInputReaction::pqChangePipelineInputReaction(
  QAction* parentObject)
: Superclass(parentObject)
{
  pqApplicationCore* core = pqApplicationCore::instance();
  QObject::connect(core->getSelectionModel(),
    SIGNAL(
      selectionChanged(const pqServerManagerSelection&,
        const pqServerManagerSelection&)),
    this, SLOT(updateEnableState()));
  this->updateEnableState();
}

//-----------------------------------------------------------------------------
void pqChangePipelineInputReaction::updateEnableState()
{
  pqServerManagerSelectionModel* selModel=
    pqApplicationCore::instance()->getSelectionModel();
  const pqServerManagerSelection& selection = *(selModel->selectedItems());
  if (selection.size() != 1)
    {
    this->parentAction()->setEnabled(false);
    return;
    }
  pqPipelineFilter* filter = qobject_cast<pqPipelineFilter*>(selection[0]);
  if (filter == NULL || filter->modifiedState() == pqProxy::UNINITIALIZED)
    {
    this->parentAction()->setEnabled(false);
    return;
    }

  this->parentAction()->setEnabled(true);
}

//-----------------------------------------------------------------------------
void pqChangePipelineInputReaction::changeInput()
{
  pqServerManagerSelectionModel* selModel=
    pqApplicationCore::instance()->getSelectionModel();
  const pqServerManagerSelection& selection = *(selModel->selectedItems());

  // The change input dialog only supports one filter at a time.
  if (selection.size() != 1)
    {
    qCritical() << "No active selection.";
    return;
    }

  pqPipelineFilter *filter = 
    qobject_cast<pqPipelineFilter *>(selection[0]);
  if (!filter)
    {
    qCritical() << "No active filter.";
    return;
    }

  pqFilterInputDialog dialog(pqCoreUtilities::mainWidget());
  dialog.setObjectName("ChangeInputDialog");
  pqServerManagerModel *smModel =
    pqApplicationCore::instance()->getServerManagerModel();
  pqPipelineModel model(*smModel);
  dialog.setModelAndFilter(&model, filter, filter->getNamedInputs());
  if (QDialog::Accepted == dialog.exec())
    {
    BEGIN_UNDO_SET(QString("Change Input for %1").arg(
        filter->getSMName()));
    for (int cc=0; cc < filter->getNumberOfInputPorts(); cc++)
      {
      QString inputPortName = filter->getInputPortName(cc);
      QList<pqOutputPort*> inputs = dialog.getFilterInputs(inputPortName);

      vtkstd::vector<vtkSMProxy*> inputPtrs;
      vtkstd::vector<unsigned int> inputPorts;

      foreach (pqOutputPort* opport, inputs)
        {
        inputPtrs.push_back(opport->getSource()->getProxy());
        inputPorts.push_back(opport->getPortNumber());
        }

      vtkSMInputProperty* ip =vtkSMInputProperty::SafeDownCast(
        filter->getProxy()->GetProperty(
          inputPortName.toAscii().data()));
      ip->SetProxies(inputPtrs.size(), &inputPtrs[0], &inputPorts[0]);
      }
    filter->getProxy()->UpdateVTKObjects();
    END_UNDO_SET();

    // render all views
    pqApplicationCore::instance()->render();
    }
}

