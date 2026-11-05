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

#include "UBPenPanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>

#include "core/UBSettings.h"
#include "gui/UBStylusPalette.h"

#include "core/memcheck.h"

namespace
{
    // distance between the glass body and the top edge of the board
    constexpr int anchorGap = 0;
}

UBPenPanel::UBPenPanel(QWidget* parent)
    : UBFloatingPalette(Qt::TopLeftCorner, parent)
    , mLayout(nullptr)
    , mRepositionPending(false)
{
    setObjectName("penPanel");

    mLayout = new QHBoxLayout(this);
    const int b = border();
    mLayout->setContentsMargins(b + 10, b + 7, b + 10, b + 7);
    mLayout->setSpacing(6);
    mLayout->setSizeConstraint(QLayout::SetFixedSize);

    connect(UBSettings::settings()->appToolBarPositionedAtTop, SIGNAL(changed(QVariant)), this, SLOT(reposition()));
}


UBPenPanel::~UBPenPanel()
{
    // NOOP
}


void UBPenPanel::addSection(QWidget* section)
{
    if (!section)
        return;

    QFrame* divider = new QFrame(this);
    divider->setObjectName("glassDivider");
    divider->setFrameShape(QFrame::NoFrame);
    divider->setFixedWidth(1);
    divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    section->setParent(this);
    mLayout->addWidget(divider);
    mLayout->addWidget(section, 0, Qt::AlignVCenter);

    section->installEventFilter(this);

    mSections << section;
    mDividers << divider;

    updateDividers();
}


void UBPenPanel::updateDividers()
{
    bool previousVisible = false;

    for (int i = 0; i < mSections.size(); ++i)
    {
        const bool visible = !mSections.at(i)->isHidden();
        mDividers.at(i)->setVisible(visible && previousVisible);
        previousVisible = previousVisible || visible;
    }
}


void UBPenPanel::reposition()
{
    QWidget* parent = parentWidget();

    if (!parent)
        return;

    int x = pos().x();
    int y = pos().y();

    if (!mCustomPosition)
    {
        x = (parent->width() - width()) / 2;

        if (UBSettings::settings()->appToolBarPositionedAtTop->get().toBool())
        {
            // just below the top bar
            y = anchorGap;
        }
        else
        {
            // just inside the bottom edge, above the stylus palette when it is there
            y = parent->height() - height();

            UBStylusPalette* stylusPalette = parent->findChild<UBStylusPalette*>(QString(), Qt::FindDirectChildrenOnly);

            if (stylusPalette && stylusPalette->isVisible()
                    && !UBSettings::settings()->appToolBarOrientationVertical->get().toBool())
            {
                y = qMin(y, stylusPalette->y() - height() + border());
            }
        }
    }

    // keep the panel inside the board
    x = qBound(0, x, qMax(0, parent->width() - width()));
    y = qBound(0, y, qMax(0, parent->height() - height()));

    if (QPoint(x, y) != pos())
        move(x, y);
}


bool UBPenPanel::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Resize:
        case QEvent::Show:
            if (!mRepositionPending)
            {
                // after the layout settled
                mRepositionPending = true;
                QMetaObject::invokeMethod(this, [this]() {
                    mRepositionPending = false;
                    reposition();
                }, Qt::QueuedConnection);
            }
            break;

        default:
            break;
    }

    return UBFloatingPalette::event(event);
}


bool UBPenPanel::eventFilter(QObject* watched, QEvent* event)
{
    if ((event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)
            && mSections.contains(static_cast<QWidget*>(watched)))
    {
        // the visibility flag is final once the event is processed
        QMetaObject::invokeMethod(this, &UBPenPanel::updateDividers, Qt::QueuedConnection);
    }

    return UBFloatingPalette::eventFilter(watched, event);
}


void UBPenPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    // re-anchor the panel after it was dragged away
    setCustomPosition(false);
    reposition();
    event->accept();
}


int UBPenPanel::border()
{
    // room for the drop shadow
    return 10;
}


int UBPenPanel::radius()
{
    // capsule
    return 24;
}
