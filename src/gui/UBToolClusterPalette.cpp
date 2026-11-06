/*
 * Copyright (C) 2026 OpenBoard-ML contributors
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

#include "UBToolClusterPalette.h"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>

#include "core/UBSettings.h"
#include "gui/UBGlassEffect.h"
#include "gui/UBGlassStyle.h"

#include "core/memcheck.h"

UBToolClusterPalette::UBToolClusterPalette(Qt::Corner anchor, QWidget* parent)
    : UBFloatingPalette(anchor, parent)
    , mAnchor(anchor)
    , mLayout(nullptr)
    , mTextUnderIcon(false)
    , mRepositionPending(false)
{
    setObjectName("toolCluster");
    setProperty("glassLabels", false);

    mLayout = new QHBoxLayout(this);
    const int b = border();
    mLayout->setContentsMargins(b + 6, b + 6, b + 6, b + 6);
    mLayout->setSpacing(2);
    mLayout->setSizeConstraint(QLayout::SetFixedSize);

    if (parent)
        parent->installEventFilter(this);

    connect(UBSettings::settings()->appToolBarPositionedAtTop, &UBSetting::changed, this, [this]() {
        scheduleReposition();
    });
}


UBToolClusterPalette::~UBToolClusterPalette()
{
    // NOOP
}


void UBToolClusterPalette::addGroup(const QList<QAction*>& actions)
{
    if (actions.isEmpty())
        return;

    if (!mGroups.isEmpty())
    {
        QFrame* separator = new QFrame(this);
        separator->setObjectName("clusterSeparator");
        separator->setFrameShape(QFrame::NoFrame);
        separator->setFixedWidth(1);
        separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        mLayout->addSpacing(4);
        mLayout->addWidget(separator);
        mLayout->addSpacing(4);
        mSeparators << separator;
    }

    QList<QToolButton*> group;

    for (QAction* action : actions)
    {
        QToolButton* button = new QToolButton(this);
        button->setObjectName("clusterButton");
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setDefaultAction(action);
        button->setIconSize(QSize(UBGlassStyle::iconSize, UBGlassStyle::iconSize));
        button->setToolButtonStyle(mTextUnderIcon ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);

        if (action->menu())
            button->setPopupMode(QToolButton::InstantPopup);

        // the button follows the visibility of its action like a toolbar does
        connect(action, &QAction::changed, this, &UBToolClusterPalette::updateButtonVisibility);

        mLayout->addWidget(button);
        group << button;
    }

    mGroups << group;
    updateButtonVisibility();
}


QToolButton* UBToolClusterPalette::buttonForAction(QAction* action) const
{
    for (const QList<QToolButton*>& group : mGroups)
    {
        for (QToolButton* button : group)
        {
            if (button->defaultAction() == action)
                return button;
        }
    }

    return nullptr;
}


QList<QRect> UBToolClusterPalette::clusterBodies(QWidget* parent)
{
    QList<QRect> bodies;

    if (!parent)
        return bodies;

    for (UBToolClusterPalette* cluster : parent->findChildren<UBToolClusterPalette*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (cluster->isVisible())
        {
            const int b = cluster->border();
            bodies << cluster->geometry().adjusted(b, b, -b, -b);
        }
    }

    return bodies;
}


void UBToolClusterPalette::setTextUnderIcon(bool textUnderIcon)
{
    mTextUnderIcon = textUnderIcon;
    setProperty("glassLabels", textUnderIcon);

    for (const QList<QToolButton*>& group : std::as_const(mGroups))
    {
        for (QToolButton* button : group)
        {
            button->setToolButtonStyle(textUnderIcon ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }

    // labelled buttons are taller: a rounded rectangle instead of a capsule
    update();
    adjustSize();
    scheduleReposition();
}


void UBToolClusterPalette::updateButtonVisibility()
{
    bool previousGroupVisible = false;

    for (int g = 0; g < mGroups.size(); ++g)
    {
        bool groupVisible = false;

        for (QToolButton* button : mGroups.at(g))
        {
            const bool visible = button->defaultAction() && button->defaultAction()->isVisible();
            button->setVisible(visible);
            groupVisible = groupVisible || visible;
        }

        if (g > 0)
            mSeparators.at(g - 1)->setVisible(groupVisible && previousGroupVisible);

        previousGroupVisible = previousGroupVisible || groupVisible;
    }

    scheduleReposition();
}


void UBToolClusterPalette::scheduleReposition()
{
    if (mRepositionPending)
        return;

    // after the layout settled
    mRepositionPending = true;
    QMetaObject::invokeMethod(this, [this]() {
        mRepositionPending = false;
        reposition();
    }, Qt::QueuedConnection);
}


void UBToolClusterPalette::reposition()
{
    QWidget* parent = parentWidget();

    if (!parent)
        return;

    const int b = border();
    const bool atTop = UBSettings::settings()->appToolBarPositionedAtTop->get().toBool();
    const bool left = (mAnchor == Qt::TopLeftCorner || mAnchor == Qt::BottomLeftCorner);

    int x = left ? edgeGap - b : parent->width() - width() - edgeGap + b;
    int y = atTop ? UBGlassEffect::topInset(parent) + edgeGap - b : parent->height() - height() - edgeGap + b;

    x = qBound(-b, x, qMax(-b, parent->width() - width() + b));
    y = qBound(-b, y, qMax(-b, parent->height() - height() + b));

    if (QPoint(x, y) != pos() || size() != mLastSize)
    {
        mLastSize = size();
        move(x, y);
        emit repositioned();
    }
}


bool UBToolClusterPalette::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::ParentChange:
            scheduleReposition();
            break;

        default:
            break;
    }

    return UBFloatingPalette::event(event);
}


bool UBToolClusterPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        reposition();

    return UBFloatingPalette::eventFilter(watched, event);
}


void UBToolClusterPalette::mousePressEvent(QMouseEvent* event)
{
    // anchored like a toolbar: no dragging
    event->accept();
}


void UBToolClusterPalette::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}


void UBToolClusterPalette::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}


int UBToolClusterPalette::border()
{
    // room for the drop shadow
    return UBGlassStyle::shadowMargin;
}


int UBToolClusterPalette::radius()
{
    // capsule, a large rounded rectangle when the buttons show labels
    return mTextUnderIcon ? 22 : 1000;
}
