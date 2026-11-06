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

#include "UBPaletteSwitchButton.h"

#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include "core/UBColorPalette.h"
#include "core/UBSettings.h"
#include "gui/UBColorPaletteDialog.h"
#include "gui/UBGlassEffect.h"
#include "gui/UBGlassStyle.h"
#include "gui/UBUiStyle.h"

#include "core/memcheck.h"

namespace
{
    // the row of colours painted at the right of each palette entry
    constexpr int dotSize = 9;
    constexpr int dotPitch = 12;
    constexpr int dotsTrailing = 14;   // from the right edge of the entry
    constexpr int dotsLeading = 18;    // gap between the name and the colours

    constexpr int holdInterval = 500;  // press and hold opens the menu (stylus)

    int dotsWidth(int count)
    {
        return count > 0 ? (count - 1) * dotPitch + dotSize : 0;
    }

    /** colours of a palette as shown by the swatches: tool and page background */
    QList<QColor> displayedColors(const UBColorPalette& palette)
    {
        const bool dark = UBSettings::settings()->isDarkBackground();

        if (UBColorPaletteManager::manager()->isMarkerToolActive())
            return dark ? palette.markerDark : palette.markerLight;

        return dark ? palette.penDark : palette.penLight;
    }

    /**
     * Menu that paints a row of colour dots at the right of the palette entries
     * (actions with the property "ubPaletteColors"); the room for the dots is
     * reserved with the right padding of the items.
     */
    class UBPaletteSwitchMenu : public QMenu
    {
        public:
            explicit UBPaletteSwitchMenu(QWidget* parent)
                : QMenu(parent)
            {
                setObjectName("paletteSwitchMenu");
            }

        protected:
            void paintEvent(QPaintEvent* event) override
            {
                QMenu::paintEvent(event);

                QPainter painter(this);
                painter.setRenderHint(QPainter::Antialiasing);

                for (QAction* action : actions())
                {
                    const QVariant colorsValue = action->property("ubPaletteColors");

                    if (!colorsValue.isValid())
                        continue;

                    const QRect item = actionGeometry(action);

                    if (!item.intersects(event->rect()))
                        continue;

                    const QVariantList colors = colorsValue.toList();
                    const int columnWidth = property("ubDotsColumn").toInt();
                    qreal x = item.right() + 1 - dotsTrailing - columnWidth;
                    const qreal y = item.top() + (item.height() - dotSize) / 2.0;

                    for (const QVariant& value : colors)
                    {
                        const QColor color = value.value<QColor>();
                        const QRectF dot(x + 0.5, y + 0.5, dotSize - 1, dotSize - 1);

                        // flat dot with the neutral hairline of the swatches
                        painter.setBrush(color);
                        painter.setPen(QPen(QColor(128, 128, 128, 110), 1.0));
                        painter.drawEllipse(dot);

                        x += dotPitch;
                    }
                }
            }
    };
}


UBPaletteSwitchButton::UBPaletteSwitchButton(QAction* nextPaletteAction, QWidget* parent)
    : QToolButton(parent)
    , mNextPaletteAction(nextPaletteAction)
    , mHoldTimer(new QTimer(this))
    , mHeld(false)
{
    setObjectName("paletteSwitchButton");
    setFocusPolicy(Qt::NoFocus);
    setAutoRaise(true);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIcon(UBGlassStyle::icon("paletteSwitch"));
    setIconSize(QSize(18, 18));

    mHoldTimer->setSingleShot(true);
    mHoldTimer->setInterval(holdInterval);
    connect(mHoldTimer, &QTimer::timeout, this, &UBPaletteSwitchButton::holdTimeout);

    connect(this, &QToolButton::clicked, this, &UBPaletteSwitchButton::nextPalette);

    UBColorPaletteManager* manager = UBColorPaletteManager::manager();
    connect(manager, &UBColorPaletteManager::palettesChanged, this, &UBPaletteSwitchButton::refresh);
    connect(manager, &UBColorPaletteManager::activePaletteChanged, this, &UBPaletteSwitchButton::refresh);

    refresh();
}


UBPaletteSwitchButton::~UBPaletteSwitchButton()
{
    // NOOP
}


void UBPaletteSwitchButton::refresh()
{
    UBColorPaletteManager* manager = UBColorPaletteManager::manager();

    // nothing to switch between with a single palette
    setVisible(manager->palettes().size() > 1);

    setToolTip(tr("Palette: %1 · click for next").arg(manager->activePalette().name)
               + QLatin1Char('\n') + tr("Right-click or hold for all palettes"));
}


void UBPaletteSwitchButton::nextPalette()
{
    // through the action, so that the action, its shortcut and the button stay one path
    if (mNextPaletteAction)
        mNextPaletteAction->trigger();
    else
        UBColorPaletteManager::manager()->selectNextPalette();
}


void UBPaletteSwitchButton::mousePressEvent(QMouseEvent* event)
{
    mHeld = false;

    if (event->button() == Qt::LeftButton)
        mHoldTimer->start();

    QToolButton::mousePressEvent(event);
}


void UBPaletteSwitchButton::mouseMoveEvent(QMouseEvent* event)
{
    if (mHoldTimer->isActive() && !rect().contains(event->position().toPoint()))
        mHoldTimer->stop();

    QToolButton::mouseMoveEvent(event);
}


void UBPaletteSwitchButton::mouseReleaseEvent(QMouseEvent* event)
{
    mHoldTimer->stop();

    if (mHeld)
    {
        // the menu was opened by holding the button: no palette switch
        mHeld = false;
        event->accept();
        return;
    }

    QToolButton::mouseReleaseEvent(event);
}


void UBPaletteSwitchButton::contextMenuEvent(QContextMenuEvent* event)
{
    mHoldTimer->stop();
    showPaletteMenu();
    event->accept();
}


void UBPaletteSwitchButton::holdTimeout()
{
    if (!isDown())
        return;

    mHeld = true;

    // release the button without triggering it
    setDown(false);
    showPaletteMenu();
}


void UBPaletteSwitchButton::showPaletteMenu()
{
    // already open (e.g. a long press followed by a context menu event of the platform)
    if (mMenu && mMenu->isVisible())
        return;

    UBColorPaletteManager* manager = UBColorPaletteManager::manager();

    // pending edits of the active palette (e.g. a recoloured swatch) are shown too
    manager->captureFromSettings();

    if (!mMenu)
    {
        mMenu = new UBPaletteSwitchMenu(window());

        // native menu material on macOS (glass style)
        if (UBUiStyle::isGlass())
            UBGlassEffect::applyToMenu(mMenu);

        mMenu->setProperty("ubBaseStyleSheet", mMenu->styleSheet());
    }

    mMenu->clear();
    qDeleteAll(mMenu->findChildren<QActionGroup*>(QString(), Qt::FindDirectChildrenOnly));

    const QList<UBColorPalette> palettes = manager->palettes();
    const int active = manager->activeIndex();

    QActionGroup* group = new QActionGroup(mMenu);
    group->setExclusive(true);

    int maxColors = 0;

    for (int i = 0; i < palettes.size(); ++i)
    {
        const UBColorPalette& palette = palettes.at(i);
        QString name = palette.name;
        name.replace(QLatin1Char('&'), QLatin1String("&&"));

        QAction* action = mMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(i == active);
        group->addAction(action);

        QVariantList colors;

        for (const QColor& color : displayedColors(palette))
            colors << color;

        maxColors = qMax(maxColors, static_cast<int>(colors.size()));
        action->setProperty("ubPaletteColors", colors);

        connect(action, &QAction::triggered, manager, [manager, i]() {
            manager->setActivePalette(i);
        });
    }

    mMenu->addSeparator();

    QAction* manageAction = mMenu->addAction(tr("Manage palettes..."));
    QWidget* dialogParent = window();
    connect(manageAction, &QAction::triggered, this, [dialogParent]() {
        // after the menu closed
        QTimer::singleShot(0, dialogParent, [dialogParent]() {
            UBColorPaletteDialog::showManager(dialogParent);
        });
    });

    // room for the row of colours at the right of the entries
    const int column = dotsWidth(maxColors);
    mMenu->setProperty("ubDotsColumn", column);
    mMenu->setStyleSheet(mMenu->property("ubBaseStyleSheet").toString()
                         + QString("QMenu::item { padding-right: %1px; }").arg(column + dotsLeading + dotsTrailing));

    // below the button, above it when the pen panel is at the bottom of the board
    const bool atTop = UBSettings::settings()->appToolBarPositionedAtTop->get().toBool();
    const QSize menuSize = mMenu->sizeHint();
    const QPoint position = atTop
            ? mapToGlobal(QPoint(0, height() + 14))
            : mapToGlobal(QPoint(0, -menuSize.height() - 14));

    mMenu->popup(position);
}
