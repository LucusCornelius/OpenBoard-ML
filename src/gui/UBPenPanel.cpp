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
#include "gui/UBGlassStyle.h"
#include "gui/UBStylusPalette.h"
#include "gui/UBToolClusterPalette.h"
#include "gui/UBGlassEffect.h"

#include "core/memcheck.h"

UBPenPanel::UBPenPanel(QWidget* parent)
    : UBFloatingPalette(Qt::TopLeftCorner, parent)
    , mLayout(nullptr)
    , mRepositionPending(false)
{
    setObjectName("penPanel");

    mLayout = new QHBoxLayout(this);
    const int b = border();
    mLayout->setContentsMargins(b + 8, b + 6, b + 8, b + 6);
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
    const int b = border();

    if (!mCustomPosition)
    {
        // centred between the tool clusters, on the same row; on narrow boards
        // where it would touch a cluster, it moves next to the stylus palette
        const QList<QRect> clusters = UBToolClusterPalette::clusterBodies(parent);
        const int gap = UBToolClusterPalette::edgeGap;
        const bool atTop = UBSettings::settings()->appToolBarPositionedAtTop->get().toBool();

        x = (parent->width() - width()) / 2;
        y = atTop ? UBGlassEffect::topInset(parent) + gap - b : parent->height() - height() - gap + b;

        auto collides = [&clusters, gap, b, this](int px, int py) {
            const QRect body = QRect(px, py, width(), height()).adjusted(b, b, -b, -b);

            for (const QRect& cluster : clusters)
            {
                if (cluster.adjusted(-gap, 0, gap, 0).intersects(body))
                    return true;
            }

            return false;
        };

        if (collides(x, y))
        {
            // centred in the free space between the clusters, if it fits there
            int freeLeft = 0;
            int freeRight = parent->width();

            for (const QRect& cluster : clusters)
            {
                if (cluster.center().x() < parent->width() / 2)
                    freeLeft = qMax(freeLeft, cluster.right() + 1);
                else
                    freeRight = qMin(freeRight, cluster.left());
            }

            x = (freeLeft + freeRight - width()) / 2;
        }

        if (collides(x, y))
        {
            x = (parent->width() - width()) / 2;

            if (atTop)
            {
                int bottom = 0;

                for (const QRect& cluster : clusters)
                    bottom = qMax(bottom, cluster.bottom());

                y = bottom + gap - b;
            }
            else
            {
                UBStylusPalette* stylusPalette = parent->findChild<UBStylusPalette*>(QString(), Qt::FindDirectChildrenOnly);
                int top = parent->height();

                for (const QRect& cluster : clusters)
                    top = qMin(top, cluster.top());

                if (stylusPalette && stylusPalette->isVisible()
                        && !UBSettings::settings()->appToolBarOrientationVertical->get().toBool())
                {
                    top = qMin(top, stylusPalette->y() + static_cast<UBFloatingPalette*>(stylusPalette)->border());
                }

                y = top - gap - height() + b;
            }
        }
    }

    // keep the panel inside the board, the shadow margin may leave it
    x = qBound(-b, x, qMax(-b, parent->width() - width() + b));
    y = qBound(-b, y, qMax(-b, parent->height() - height() + b));

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
    return UBGlassStyle::shadowMargin;
}


int UBPenPanel::radius()
{
    // capsule
    return 1000;
}
