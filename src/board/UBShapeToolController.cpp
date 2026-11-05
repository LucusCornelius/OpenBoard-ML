/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */




#include "UBShapeToolController.h"

#include <QAction>
#include <QToolBar>

#include "core/UB.h"
#include "core/UBSettings.h"

#include "board/UBDrawingController.h"

#include "domain/UBGraphicsShapeItem.h"

#include "gui/UBMainWindow.h"
#include "gui/UBToolbarButtonGroup.h"

#include "core/memcheck.h"

namespace
{
    const QString shapeKindSetting("Board/ShapeKind");
    const QString shapeFillSetting("Board/ShapeFill");
}

UBShapeToolController::UBShapeToolController(UBMainWindow* mainWindow, QToolBar* toolBar, QAction* insertBefore, QObject* parent)
    : QObject(parent)
    , mMainWindow(mainWindow)
    , mKindChoice(nullptr)
    , mKindChoiceAction(nullptr)
    , mFillAction(mainWindow->actionShapeFill)
{
    // order must match UBGraphicsShapeItem::Kind
    mKindActions << mainWindow->actionShapeRect
                 << mainWindow->actionShapeRoundedRect
                 << mainWindow->actionShapeEllipse
                 << mainWindow->actionShapeTriangle
                 << mainWindow->actionShapeLine
                 << mainWindow->actionShapeArrow;

    mKindChoice = new UBToolbarButtonGroup(toolBar, mKindActions);
    mKindChoice->setLabel(tr("Shape"));

    const int kind = currentKind();
    mKindChoice->setCurrentIndex(kind);
    mKindActions.at(kind)->setChecked(true);

    mKindChoiceAction = toolBar->insertWidget(insertBefore, mKindChoice);
    toolBar->insertAction(insertBefore, mFillAction);

    mFillAction->setChecked(fillEnabled());

    UBSettings* settings = UBSettings::settings();
    mKindChoice->displayText(QVariant(settings->appToolBarDisplayText->get().toBool()));

    connect(settings->appToolBarDisplayText, SIGNAL(changed(QVariant)), this, SLOT(displayText(QVariant)));
    connect(mKindChoice, SIGNAL(activated(int)), this, SLOT(kindActivated(int)));
    connect(mFillAction, SIGNAL(triggered(bool)), this, SLOT(fillToggled(bool)));
    connect(UBDrawingController::drawingController(), SIGNAL(stylusToolChanged(int,int)), this, SLOT(stylusToolChanged(int,int)));

    updateVisibility(UBDrawingController::drawingController()->stylusTool());
}

UBShapeToolController::~UBShapeToolController()
{
}

int UBShapeToolController::currentKind()
{
    const int kind = UBSettings::settings()->value(shapeKindSetting, int(UBGraphicsShapeItem::Rectangle)).toInt();
    return qBound(0, kind, int(UBGraphicsShapeItem::KindCount) - 1);
}

bool UBShapeToolController::fillEnabled()
{
    return UBSettings::settings()->value(shapeFillSetting, false).toBool();
}

void UBShapeToolController::kindActivated(int index)
{
    UBSettings::settings()->setValue(shapeKindSetting, index);

    // choosing a shape activates the shapes tool
    if (UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Shape)
    {
        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Shape);
    }
}

void UBShapeToolController::fillToggled(bool checked)
{
    UBSettings::settings()->setValue(shapeFillSetting, checked);

    if (UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Shape)
    {
        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Shape);
    }
}

void UBShapeToolController::stylusToolChanged(int tool, int previousTool)
{
    Q_UNUSED(previousTool);
    updateVisibility(tool);
}

void UBShapeToolController::displayText(const QVariant& display)
{
    mKindChoice->displayText(display);
}

void UBShapeToolController::updateVisibility(int tool)
{
    const bool visible = (tool == UBStylusTool::Shape);

    if (mKindChoiceAction)
        mKindChoiceAction->setVisible(visible);

    mFillAction->setVisible(visible);
}
