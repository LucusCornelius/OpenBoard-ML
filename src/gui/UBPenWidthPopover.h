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

#ifndef UBPENWIDTHPOPOVER_H_
#define UBPENWIDTHPOPOVER_H_

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QPointer>

#include "UBFloatingPalette.h"

class QLabel;
class QSlider;
class QToolButton;
class UBSetting;
class UBToolbarButtonGroup;
class UBPenWidthPopoverFilter;

/**
 * A short stroke of the given width at its size on the board, or the eraser
 * circle, on the colour of the page.
 */
class UBStrokeWidthPreview : public QWidget
{
    Q_OBJECT

    public:
        explicit UBStrokeWidthPreview(QWidget* parent = nullptr);

        void setStroke(qreal width, const QColor& color, bool eraser, bool darkPage);

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        qreal mWidth{1.0};
        QColor mColor{Qt::black};
        bool mEraser{false};
        bool mDarkPage{false};
};


/**
 * Glass popover to adjust the width presets of the pen panel. A press on the
 * width button that is already selected opens it next to the pen panel, with a
 * slider, a preview at the size on the board, the value and a reset to the
 * default. It edits the preset of that button for the current tool (pen,
 * marker or eraser) in the settings that Preferences uses, so the next stroke
 * uses the new width and it is saved with the other settings.
 *
 * The popover closes on a press anywhere outside it, which is not consumed (a
 * stroke started there is drawn), on Escape, when the tool changes and when
 * another preset gets selected. It also keeps the glyphs of the width buttons
 * in step with the preset values.
 */
class UBPenWidthPopover : public UBFloatingPalette
{
    Q_OBJECT

    public:
        /** watch the buttons of a width group of the pen panel (eraser: the eraser widths) */
        static void attach(UBToolbarButtonGroup* group, bool eraser);

        int border() override;

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        friend class UBPenWidthPopoverFilter;

        enum Tool
        {
            Pen,
            Marker,
            Eraser
        };

        explicit UBPenWidthPopover(QWidget* parent);

        void watchButtons(UBToolbarButtonGroup* group, bool eraser);
        bool buttonEvent(QToolButton* button, QEvent* event);
        bool outsideEvent(QObject* watched, QEvent* event);
        bool canOpenFrom(QToolButton* button) const;
        void open(QToolButton* button);
        void dismiss();
        void reposition();
        void refresh();
        void showWidth(qreal width);
        void sliderChanged(int position);
        void resetPreset();
        void updateIcons();

        static UBSetting* presetSetting(Tool tool, int preset);
        static qreal presetWidth(Tool tool, int preset);
        void setPresetWidth(qreal width);
        qreal defaultWidth() const;
        qreal lowerLimit() const;
        qreal upperLimit() const;
        qreal stepsPerPixel() const;
        int sliderPosition(qreal width) const;
        qreal sliderWidth(int position) const;

        static QPointer<UBPenWidthPopover> sInstance;

        UBPenWidthPopoverFilter* mOutsideFilter{nullptr};
        QPointer<QWidget> mPanel;
        QList<QToolButton*> mLineButtons;
        QList<QToolButton*> mEraserButtons;
        QPointer<QToolButton> mAnchor;
        QPointer<QToolButton> mArmedButton;
        QPointer<QToolButton> mClosedByButton;
        QElapsedTimer mSelectionAge;
        Tool mTool{Pen};
        int mPreset{0};
        bool mUpdating{false};
        bool mRepositionPending{false};

        QLabel* mTitle{nullptr};
        QLabel* mPresetLabel{nullptr};
        QLabel* mValue{nullptr};
        UBStrokeWidthPreview* mPreview{nullptr};
        QSlider* mSlider{nullptr};
        QToolButton* mResetButton{nullptr};
};

#endif /* UBPENWIDTHPOPOVER_H_ */
