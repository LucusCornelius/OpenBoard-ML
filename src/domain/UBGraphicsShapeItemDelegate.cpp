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




#include "UBGraphicsShapeItemDelegate.h"

#include <QMenu>
#include <QColorDialog>

#include "domain/UBGraphicsShapeItem.h"
#include "domain/UBGraphicsDelegateFrame.h"
#include "domain/UBGraphicsScene.h"

#include "core/UBApplication.h"
#include "core/UBSettings.h"

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"

#include "core/memcheck.h"

UBGraphicsShapeItemDelegate::UBGraphicsShapeItemDelegate(UBGraphicsShapeItem* pDelegated, QObject* parent)
    : UBGraphicsItemDelegate(pDelegated, parent, GF_COMMON | GF_REVOLVABLE)
    , mFillAction(nullptr)
    , mStartArrowAction(nullptr)
    , mEndArrowAction(nullptr)
{
}

UBGraphicsShapeItemDelegate::~UBGraphicsShapeItemDelegate()
{
}

void UBGraphicsShapeItemDelegate::createControls()
{
    UBGraphicsItemDelegate::createControls();

    // resizing changes the geometry of the shape, so that the stroke width is kept
    frame()->setOperationMode(UBGraphicsDelegateFrame::Resizing);
}

UBGraphicsShapeItem* UBGraphicsShapeItemDelegate::shapeItem() const
{
    return static_cast<UBGraphicsShapeItem*>(mDelegated);
}

void UBGraphicsShapeItemDelegate::decorateMenu(QMenu* menu)
{
    UBGraphicsItemDelegate::decorateMenu(menu);

    menu->addSeparator();

    menu->addAction(tr("Line Color..."), this, SLOT(pickStrokeColor()));

    QMenu* widthMenu = menu->addMenu(tr("Line Width"));
    widthMenu->addAction(tr("Thin"), this, SLOT(setThinStroke()));
    widthMenu->addAction(tr("Medium"), this, SLOT(setMediumStroke()));
    widthMenu->addAction(tr("Thick"), this, SLOT(setThickStroke()));

    if (shapeItem()->isFillable())
    {
        mFillAction = menu->addAction(tr("Filled"), this, SLOT(setFilled(bool)));
        mFillAction->setCheckable(true);
        menu->addAction(tr("Fill Color..."), this, SLOT(pickFillColor()));
    }
    else
    {
        mStartArrowAction = menu->addAction(tr("Arrow at Start"), this, SLOT(setStartArrow(bool)));
        mStartArrowAction->setCheckable(true);
        mEndArrowAction = menu->addAction(tr("Arrow at End"), this, SLOT(setEndArrow(bool)));
        mEndArrowAction->setCheckable(true);
    }
}

void UBGraphicsShapeItemDelegate::updateMenuActionState()
{
    UBGraphicsItemDelegate::updateMenuActionState();

    if (mFillAction)
        mFillAction->setChecked(shapeItem()->isFilled());

    if (mStartArrowAction)
        mStartArrowAction->setChecked(shapeItem()->hasStartArrow());

    if (mEndArrowAction)
        mEndArrowAction->setChecked(shapeItem()->hasEndArrow());
}

void UBGraphicsShapeItemDelegate::contentChanged()
{
    std::shared_ptr<UBGraphicsScene> scene = castUBGraphicsScene();

    if (scene)
        scene->setModified(true);

    positionHandles();
}

void UBGraphicsShapeItemDelegate::setFilled(bool filled)
{
    shapeItem()->setFilled(filled);
    contentChanged();
}

QColor UBGraphicsShapeItemDelegate::pickColor(const QColor& initial, const QString& title, bool withAlpha)
{
    QColorDialog colorDialog(initial, UBApplication::boardController->controlView());
    colorDialog.setWindowTitle(title);
    colorDialog.setOption(QColorDialog::DontUseNativeDialog);
    colorDialog.setOption(QColorDialog::ShowAlphaChannel, withAlpha);

    if (UBSettings::settings()->isDarkBackground())
    {
        colorDialog.setStyleSheet("background-color: white;");
    }

    if (colorDialog.exec())
    {
        return colorDialog.selectedColor();
    }

    return QColor();
}

void UBGraphicsShapeItemDelegate::pickStrokeColor()
{
    std::shared_ptr<UBGraphicsScene> scene = castUBGraphicsScene();
    const bool dark = scene && scene->isDarkBackground();

    QColor color = pickColor(shapeItem()->strokeColor(dark), tr("Line Color"), false);

    if (color.isValid())
    {
        // black and white are swapped on the other background, other colors are kept
        QColor otherColor = color;

        if (color == Qt::black)
            otherColor = Qt::white;
        else if (color == Qt::white)
            otherColor = Qt::black;

        if (dark)
        {
            shapeItem()->setColorOnDarkBackground(color);
            shapeItem()->setColorOnLightBackground(otherColor);
        }
        else
        {
            shapeItem()->setColorOnLightBackground(color);
            shapeItem()->setColorOnDarkBackground(otherColor);
        }

        contentChanged();
    }
}

void UBGraphicsShapeItemDelegate::pickFillColor()
{
    std::shared_ptr<UBGraphicsScene> scene = castUBGraphicsScene();
    const bool dark = scene && scene->isDarkBackground();

    QColor color = pickColor(shapeItem()->fillColor(dark), tr("Fill Color"), true);

    if (color.isValid())
    {
        shapeItem()->setFillOnDarkBackground(color);
        shapeItem()->setFillOnLightBackground(color);
        shapeItem()->setFilled(true);
        contentChanged();
    }
}

void UBGraphicsShapeItemDelegate::setStartArrow(bool arrow)
{
    shapeItem()->setArrowHeads(arrow, shapeItem()->hasEndArrow());
    contentChanged();
}

void UBGraphicsShapeItemDelegate::setEndArrow(bool arrow)
{
    shapeItem()->setArrowHeads(shapeItem()->hasStartArrow(), arrow);
    contentChanged();
}

void UBGraphicsShapeItemDelegate::setStrokeWidthIndex(int index)
{
    qreal width = 0;
    UBSettings* settings = UBSettings::settings();

    switch (index)
    {
    case 0:
        width = settings->boardPenFineWidth->get().toDouble();
        break;
    case 1:
        width = settings->boardPenMediumWidth->get().toDouble();
        break;
    default:
        width = settings->boardPenStrongWidth->get().toDouble();
        break;
    }

    shapeItem()->setStrokeWidth(width);
    contentChanged();
}

void UBGraphicsShapeItemDelegate::setThinStroke()
{
    setStrokeWidthIndex(0);
}

void UBGraphicsShapeItemDelegate::setMediumStroke()
{
    setStrokeWidthIndex(1);
}

void UBGraphicsShapeItemDelegate::setThickStroke()
{
    setStrokeWidthIndex(2);
}
