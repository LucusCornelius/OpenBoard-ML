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




#include "UBToolbarButtonGroup.h"

#include <QtGui>
#include <QLayout>
#include <QStyleOption>

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "gui/UBGlassStyle.h"

#include "board/UBDrawingController.h"

#include "core/memcheck.h"

UBToolbarButtonGroup::UBToolbarButtonGroup(QToolBar *toolBar, const QList<QAction*> &actions, QString objectNameprefix, int selectableCount)
    : QWidget(toolBar)
    , mToolButton(nullptr)
    , mActions(actions)
    , mCurrentIndex(-1)
    , mSelectableCount(selectableCount <= 0 ? actions.size() : qMin(selectableCount, actions.size()))
    , mDisplayLabel(true)
    , mActionGroup(0)
{
    if (toolBar && toolBar->layout() && toolBar->layout()->count() > 0)
    {
        mToolButton = qobject_cast<QToolButton*>(toolBar->layout()->itemAt(0)->widget());
    }

    init(objectNameprefix, QSize());
}

UBToolbarButtonGroup::UBToolbarButtonGroup(QWidget *parent, const QList<QAction*> &actions, const QString& objectNameprefix, int selectableCount, const QSize& buttonIconSize)
    : QWidget(parent)
    , mToolButton(nullptr)
    , mActions(actions)
    , mCurrentIndex(-1)
    , mSelectableCount(selectableCount <= 0 ? actions.size() : qMin(selectableCount, actions.size()))
    , mDisplayLabel(false)
    , mActionGroup(0)
{
    init(objectNameprefix, buttonIconSize);
}

void UBToolbarButtonGroup::init(QString objectNameprefix, const QSize& buttonIconSize)
{
    Q_ASSERT(mActions.size() > 0);

    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    QHBoxLayout *horizontalLayout = new QHBoxLayout();
    horizontalLayout->setSpacing(0);

    if (!mToolButton)
    {
        // hosted in a glass panel: no label, compact and evenly spaced
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        verticalLayout->setSpacing(0);
        horizontalLayout->setSpacing(2);
    }

    verticalLayout->addStretch();
    verticalLayout->addLayout(horizontalLayout);
    verticalLayout->addStretch();

    mActionGroup = new QActionGroup(this);
    mActionGroup->setExclusive(true);

    int i = 0;

    foreach(QAction *action, mActions)
    {
        if (action->isCheckable())
            mActionGroup->addAction(action);

        QToolButton *button = new QToolButton(this);
        mButtons.append(button);
        button->setDefaultAction(action);
        button->setCheckable(action->isCheckable());
        button->setFocusPolicy(Qt::NoFocus);

        if (buttonIconSize.isValid())
        {
            button->setIconSize(buttonIconSize);
        }

        if (action->objectName() == QLatin1String("actionColorPreferences"))
        {
            button->setProperty("colorPaletteConfigButton", true);
        }

        if(i == 0)
        {
            objectNameprefix.length() > 0 ?
                button->setObjectName(objectNameprefix + "-ubButtonGroupLeft")
                : button->setObjectName("ubButtonGroupLeft");
        }
        else if (i == mActions.size() - 1)
        {
            objectNameprefix.length() > 0 ?
                button->setObjectName(objectNameprefix + "-ubButtonGroupRight")
                : button->setObjectName("ubButtonGroupRight");

        }
        else
        {
            objectNameprefix.length() > 0 ?
                button->setObjectName(objectNameprefix + "-ubButtonGroupCenter")
                : button->setObjectName("ubButtonGroupCenter");
        }

        connect(button, SIGNAL(triggered(QAction*)), this, SLOT(setSelected(QAction*)));

        horizontalLayout->addWidget(button);
        mLabel = action->text();
        i++;
    }

    setSelectableCount(mSelectableCount);
}

UBToolbarButtonGroup::~UBToolbarButtonGroup()
{
    // NOOP
}

void UBToolbarButtonGroup::setLabel(const QString& label)
{
    mLabel = label;
}

void UBToolbarButtonGroup::setSelectableCount(int count)
{
    mSelectableCount = qBound(0, count, mActions.size());

    for (int i = 0; i < mButtons.size(); ++i)
    {
        QAction* action = mActions.at(i);
        bool selectable = action->isCheckable() && i < mSelectableCount;

        if (action->isCheckable())
        {
            action->setEnabled(selectable);
            mButtons.at(i)->setVisible(selectable);
        }
        else
        {
            action->setEnabled(true);
            mButtons.at(i)->setVisible(true);
        }
    }

    if (mCurrentIndex >= mSelectableCount)
    {
        mCurrentIndex = -1;
    }
}

void UBToolbarButtonGroup::setIcon(const QIcon &icon, int index)
{
    Q_ASSERT(index < mActions.size());

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, mActions.at(index)->associatedObjects())
#else
    foreach(QWidget *widget, mActions.at(index)->associatedWidgets())
#endif
    {
        QToolButton *button = qobject_cast<QToolButton*>(widget);
        if (button)
        {
            // change icon at action, so that updates of action do not overwrite the icon
            for (QAction* action : button->actions())
            {
                action->setIcon(icon);
            }
        }
    }
}

void UBToolbarButtonGroup::setColor(const QColor &color, int index)
{
    setIcon(UBGlassStyle::swatchIcon(color), index);

    // round swatches show the selection with a ring instead of a filled capsule
    if (index >= 0 && index < mButtons.size() && !mButtons.at(index)->property("colorSwatch").toBool())
    {
        QToolButton* button = mButtons.at(index);
        button->setProperty("colorSwatch", true);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void UBToolbarButtonGroup::setSelected(QAction *action)
{
    int index = mActions.indexOf(action);
    if (index < 0 || index >= mSelectableCount)
        return;

    if (!action->isCheckable())
        return;

    setCurrentIndex(index);
    emit activated(index);
}

int UBToolbarButtonGroup::currentIndex() const
{
    return mCurrentIndex;
}

void UBToolbarButtonGroup::setCurrentIndex(int index)
{
    Q_ASSERT(index < mSelectableCount || mSelectableCount == 0);

    if (mSelectableCount == 0)
        return;

    if (index != mCurrentIndex)
    {
        for(int i = 0; i < mButtons.size(); i++)
        {
            mButtons.at(i)->setChecked(i == index);
        }
        mCurrentIndex = index;
        emit currentIndexChanged(index);
    }
}

void UBToolbarButtonGroup::paintEvent(QPaintEvent *)
{
    if (!mToolButton || !mDisplayLabel)
        return;

    QPainter painter(this);
    QStyleOptionToolButton option;
    QPixmap emptyPixmap(32, 32);
    emptyPixmap.fill(Qt::transparent);
    QIcon emptyIcon(emptyPixmap);

    option.initFrom(mToolButton);

    option.text = mLabel;
    option.font = mToolButton->font();

    int pointSize = mToolButton->font().pointSize();
    if (pointSize > 0)
        option.font.setPointSize(pointSize);
    else
    {
        int pixelSize = mToolButton->font().pixelSize();
        if (pixelSize > 0)
            option.font.setPixelSize(pixelSize);
    }

    option.rect = rect();
    option.icon = emptyIcon; // non null icon is required for style()->drawControl(QStyle::CE_ToolButtonLabel, ...) to work correctly
    option.iconSize = emptyPixmap.size();
    option.toolButtonStyle = mDisplayLabel ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly;

    style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
}


void UBToolbarButtonGroup::colorPaletteChanged()
{
    setSelectableCount(qMin(UBSettings::settings()->colorPaletteSize, mActions.size()));

    bool isDarkBackground = UBSettings::settings()->isDarkBackground();

    QList<QColor> colors;

    if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Pen 
        || UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Line
        || UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Shape)
    {
        colors = UBSettings::settings()->penColors(isDarkBackground);
    }
    else if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker)
    {
        colors = UBSettings::settings()->markerColors(isDarkBackground);
    }

    for (int i = 0; i < mSelectableCount && i < colors.size(); i++)
    {
        setColor(colors.at(i), i);
    }
}

void UBToolbarButtonGroup::displayText(QVariant display)
{
    mDisplayLabel = display.toBool();
    QVBoxLayout* verticalLayout = (QVBoxLayout*)layout();
    verticalLayout->setStretch(2, mDisplayLabel ? 1 : 0);
    update();
}
