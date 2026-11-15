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

#include "UBPenWidthPopover.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include "board/UBBoardController.h"
#include "board/UBDrawingController.h"
#include "core/UB.h"
#include "core/UBApplication.h"
#include "core/UBPreferencesController.h"
#include "core/UBSettings.h"
#include "gui/UBGlassStyle.h"
#include "gui/UBModernStyle.h"
#include "gui/UBToolbarButtonGroup.h"
#include "gui/UBUiStyle.h"

#include "core/memcheck.h"

namespace
{
    // slider resolution; the width follows the square of the position, so that the
    // thin widths (where the pens are) get as much room as the thick ones
    constexpr int sliderPositions = 1000;

    // inner margin of the preview between the stroke and the edge of the page
    constexpr int previewInset = 12;

    QPointF eventPosition(QMouseEvent* event)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        return event->position();
#else
        return event->localPos();
#endif
    }

    // piecewise linear between the points of a table sorted by x
    qreal interpolate(const QList<QPointF>& table, qreal x)
    {
        if (x <= table.first().x())
            return table.first().y();

        for (int i = 1; i < table.size(); ++i)
        {
            const QPointF& a = table.at(i - 1);
            const QPointF& b = table.at(i);

            if (x <= b.x())
                return a.y() + (b.y() - a.y()) * (x - a.x()) / (b.x() - a.x());
        }

        return table.last().y();
    }

    // stroke width of the line glyph (24 px icon box) for a pen or marker width; the default
    // presets give the widths of the original glyphs, thicker strokes stay legible
    qreal lineGlyphWidth(qreal width)
    {
        static const QList<QPointF> table = {{0.5, 1.0}, {1.5, 1.5}, {3.0, 3.5}, {8.0, 6.5}, {24.0, 9.5}, {50.0, 12.0}};
        return interpolate(table, width);
    }

    // radius of the dot glyph (24 px icon box) for an eraser width, as for the line glyph
    qreal eraserGlyphRadius(qreal width)
    {
        static const QList<QPointF> table = {{4.0, 1.6}, {16.0, 2.6}, {64.0, 5.0}, {128.0, 8.0}};
        return interpolate(table, width);
    }

    QIcon widthGlyph(qreal size, bool eraser)
    {
        // a monochrome mask, tinted like the other glass glyphs
        constexpr int scale = 3;
        QImage mask(24 * scale, 24 * scale, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);

        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);

        if (eraser)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);
            painter.drawEllipse(QPointF(12.0, 12.0), size, size);
        }
        else
        {
            // keep the round caps inside the box
            const qreal x = qMax(4.0, 2.0 + size / 2.0);
            painter.setPen(QPen(Qt::black, size, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(x, 12.0), QPointF(24.0 - x, 12.0));
        }

        painter.end();

        return UBGlassStyle::tintedImageIcon(mask);
    }

    bool isLineTool(int tool)
    {
        return tool == UBStylusTool::Pen || tool == UBStylusTool::Line
                || tool == UBStylusTool::Shape || tool == UBStylusTool::Marker;
    }
}


/**
 * Application wide event filter while the popover is shown: a press elsewhere
 * closes it, Escape too. A separate object, so that the popover can tell these
 * events apart from those of the widgets it watches itself.
 */
class UBPenWidthPopoverFilter : public QObject
{
    public:
        explicit UBPenWidthPopoverFilter(UBPenWidthPopover* popover)
            : QObject(popover)
            , mPopover(popover)
        {
        }

        bool eventFilter(QObject* watched, QEvent* event) override
        {
            return mPopover->outsideEvent(watched, event);
        }

    private:
        UBPenWidthPopover* mPopover;
};


// ---------------------------------------------------------------------------
// UBStrokeWidthPreview
// ---------------------------------------------------------------------------

UBStrokeWidthPreview::UBStrokeWidthPreview(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}


void UBStrokeWidthPreview::setStroke(qreal width, const QColor& color, bool eraser, bool darkPage)
{
    mWidth = width;
    mColor = color;
    mEraser = eraser;
    mDarkPage = darkPage;
    update();
}


void UBStrokeWidthPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    UBFloatingPalette* palette = qobject_cast<UBFloatingPalette*>(parentWidget());
    const bool onDark = palette ? palette->isOnDarkMaterial() : UBGlassStyle::isDark();

    // a flat piece of the page with a hairline edge
    const qreal radius = UBUiStyle::isModern() ? UBModernStyle::panelRadius / 2.0 : 12.0;
    const QRectF page = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath pagePath;
    pagePath.addRoundedRect(page, radius, radius);

    painter.setPen(QPen(onDark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 34), 1.0));
    painter.setBrush(mDarkPage ? QColor(Qt::black) : QColor(Qt::white));
    painter.drawPath(pagePath);
    painter.setClipPath(pagePath);

    const QPointF center = page.center();

    if (mEraser)
    {
        // like the eraser circle on the board
        painter.setBrush(mDarkPage ? UBSettings::eraserBrushDarkBackground : UBSettings::eraserBrushLightBackground);
        QPen pen = mDarkPage ? UBSettings::eraserPenDarkBackground : UBSettings::eraserPenLightBackground;
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawEllipse(center, mWidth / 2.0, mWidth / 2.0);
    }
    else
    {
        // the stroke width on the board is in screen pixels, whatever the zoom
        const qreal inset = previewInset + mWidth / 2.0;
        painter.setPen(QPen(mColor, mWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(page.left() + inset, center.y()), QPointF(page.right() - inset, center.y()));
    }
}


// ---------------------------------------------------------------------------
// UBPenWidthPopover
// ---------------------------------------------------------------------------

QPointer<UBPenWidthPopover> UBPenWidthPopover::sInstance;


UBPenWidthPopover::UBPenWidthPopover(QWidget* parent)
    : UBFloatingPalette(Qt::TopLeftCorner, parent)
{
    setObjectName("penWidthPopover");

    mOutsideFilter = new UBPenWidthPopoverFilter(this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    const int b = border();
    layout->setContentsMargins(b + 14, b + 10, b + 14, b + 12);
    layout->setSpacing(8);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    QHBoxLayout* header = new QHBoxLayout();
    header->setSpacing(6);

    mTitle = new QLabel(this);
    QFont titleFont = mTitle->font();
    titleFont.setBold(true);
    mTitle->setFont(titleFont);

    mPresetLabel = new QLabel(this);
    mPresetLabel->setObjectName("glassHint");

    mValue = new QLabel(this);
    mValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // room for the widest value, so that the header does not move while dragging
    mValue->setMinimumWidth(mValue->fontMetrics().horizontalAdvance(tr("%1 px").arg(QLocale().toString(88.8, 'f', 1))));

    header->addWidget(mTitle, 0, Qt::AlignBaseline);
    header->addWidget(mPresetLabel, 0, Qt::AlignBaseline);
    header->addStretch(1);
    header->addWidget(mValue, 0, Qt::AlignBaseline);
    layout->addLayout(header);

    mPreview = new UBStrokeWidthPreview(this);
    mPreview->setFixedWidth(248);
    layout->addWidget(mPreview);

    QHBoxLayout* controls = new QHBoxLayout();
    controls->setSpacing(8);

    mSlider = new QSlider(Qt::Horizontal, this);
    mSlider->setRange(0, sliderPositions);
    mSlider->setPageStep(sliderPositions / 20);
    // keep the keyboard focus (and the shortcuts) on the board
    mSlider->setFocusPolicy(Qt::NoFocus);
    mSlider->setMinimumHeight(30);
    controls->addWidget(mSlider, 1);

    mResetButton = new QToolButton(this);
    mResetButton->setObjectName("glassButton");
    mResetButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    mResetButton->setFocusPolicy(Qt::NoFocus);
    mResetButton->setText(tr("Default"));
    mResetButton->setToolTip(tr("Restore the default width of this preset"));
    mResetButton->setMinimumSize(mResetButton->fontMetrics().horizontalAdvance(mResetButton->text()) + 24, 30);
    controls->addWidget(mResetButton, 0, Qt::AlignVCenter);
    layout->addLayout(controls);

    connect(mSlider, &QSlider::valueChanged, this, &UBPenWidthPopover::sliderChanged);
    connect(mResetButton, &QToolButton::clicked, this, &UBPenWidthPopover::resetPreset);

    connect(UBDrawingController::drawingController(), &UBDrawingController::stylusToolChanged, this, [this]() {
        dismiss();
        // pen and marker share the line width buttons
        updateIcons();
    });

    // Preferences edits the same settings
    for (Tool tool : {Pen, Marker})
    {
        for (int preset = 0; preset < 3; ++preset)
        {
            connect(presetSetting(tool, preset), &UBSetting::changed, this, [this]() {
                updateIcons();

                if (isVisible() && !mUpdating)
                    refresh();
            });
        }
    }

    hide();
}


void UBPenWidthPopover::attach(UBToolbarButtonGroup* group, bool eraser)
{
    // the group lives in the pen panel, the popover beside it in the same board container
    QWidget* panel = group ? group->parentWidget() : nullptr;
    QWidget* container = panel ? panel->parentWidget() : nullptr;

    if (!container)
        return;

    if (!sInstance)
    {
        sInstance = new UBPenWidthPopover(container);
        sInstance->mPanel = panel;
        panel->installEventFilter(sInstance);
        container->installEventFilter(sInstance);
    }

    sInstance->watchButtons(group, eraser);
}


void UBPenWidthPopover::watchButtons(UBToolbarButtonGroup* group, bool eraser)
{
    QList<QToolButton*>& buttons = eraser ? mEraserButtons : mLineButtons;

    for (QToolButton* button : group->buttons())
    {
        if (buttons.contains(button))
            continue;

        buttons << button;
        button->installEventFilter(this);

        // another preset got selected (shortcut, desktop palette): this one is no longer edited
        connect(button, &QToolButton::toggled, this, [this, button](bool checked) {
            if (!checked && button == mAnchor)
                dismiss();
        });
    }

    connect(group, &UBToolbarButtonGroup::currentIndexChanged, this, [this]() {
        mSelectionAge.restart();
    });

    updateIcons();
}


int UBPenWidthPopover::border()
{
    // room for the drop shadow, as around the pen panel
    return UBGlassStyle::shadowMargin;
}


bool UBPenWidthPopover::eventFilter(QObject* watched, QEvent* event)
{
    QToolButton* button = qobject_cast<QToolButton*>(watched);

    if (button && (mLineButtons.contains(button) || mEraserButtons.contains(button)))
        return buttonEvent(button, event);

    if ((watched == mPanel || watched == parentWidget())
            && (event->type() == QEvent::Move || event->type() == QEvent::Resize)
            && isVisible() && !mRepositionPending)
    {
        // the pen panel moved (dragged, re-anchored) or the board was resized
        mRepositionPending = true;
        QMetaObject::invokeMethod(this, [this]() {
            mRepositionPending = false;
            reposition();
        }, Qt::QueuedConnection);
    }

    return UBFloatingPalette::eventFilter(watched, event);
}


bool UBPenWidthPopover::buttonEvent(QToolButton* button, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton)
                break;

            // the press that closed the popover of this button only closes it
            const bool closedPopover = (mClosedByButton == button);
            mClosedByButton = nullptr;

            // a second press right after a selection (double click, bouncing pen tip) does not open it
            const bool settled = !mSelectionAge.isValid() || mSelectionAge.elapsed() >= QApplication::doubleClickInterval();

            mArmedButton = (!closedPopover && settled && canOpenFrom(button)) ? button : nullptr;
            break;
        }

        case QEvent::MouseButtonDblClick:
            mArmedButton = nullptr;
            mClosedByButton = nullptr;
            break;

        case QEvent::MouseButtonRelease:
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

            if (mouseEvent->button() == Qt::LeftButton && mArmedButton == button
                    && button->rect().contains(eventPosition(mouseEvent).toPoint()))
            {
                // once the button handled its click (it selects the same preset again)
                QPointer<QToolButton> pressed(button);
                QTimer::singleShot(0, this, [this, pressed]() {
                    if (pressed && canOpenFrom(pressed))
                        open(pressed);
                });
            }

            mArmedButton = nullptr;
            break;
        }

        case QEvent::Hide:
            // the pen panel or the board went away, or the tool hid the group
            if (button == mAnchor)
                dismiss();
            break;

        default:
            break;
    }

    return false;
}


bool UBPenWidthPopover::outsideEvent(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::NonClientAreaMouseButtonPress:
        case QEvent::TabletPress:
        case QEvent::TouchBegin:
        {
            if (!watched->isWidgetType())
                break;

            QWidget* widget = static_cast<QWidget*>(watched);

            if (widget != this && !isAncestorOf(widget))
            {
                if (widget == mAnchor)
                    mClosedByButton = mAnchor;

                // the event goes on: a stroke started on the board is drawn
                dismiss();
            }

            break;
        }

        case QEvent::ShortcutOverride:
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
            {
                // the key press must reach the popover, not a shortcut
                event->accept();
                return true;
            }
            break;

        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
            {
                dismiss();
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}


bool UBPenWidthPopover::canOpenFrom(QToolButton* button) const
{
    if (!button || !button->isChecked() || !button->isEnabled() || !button->isVisible())
        return false;

    // with another tool (hand, selector, text...) a press on the width only brings the pen back
    const int tool = UBDrawingController::drawingController()->stylusTool();

    if (mEraserButtons.contains(button))
        return tool == UBStylusTool::Eraser;

    return mLineButtons.contains(button) && isLineTool(tool);
}


void UBPenWidthPopover::open(QToolButton* button)
{
    const bool eraser = mEraserButtons.contains(button);
    const QList<QToolButton*>& buttons = eraser ? mEraserButtons : mLineButtons;

    mAnchor = button;
    mPreset = qBound(0, static_cast<int>(buttons.indexOf(button)), 2);

    if (eraser)
        mTool = Eraser;
    else if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker)
        mTool = Marker;
    else
        mTool = Pen;

    switch (mTool)
    {
        case Pen:
            mTitle->setText(tr("Pen width"));
            break;
        case Marker:
            mTitle->setText(tr("Marker width"));
            break;
        case Eraser:
            mTitle->setText(tr("Eraser size"));
            break;
    }

    const QStringList presetNames = {tr("Fine"), tr("Medium"), tr("Strong")};
    mPresetLabel->setText(presetNames.value(mPreset));

    // the thickest width fits at its size on the board
    mPreview->setFixedHeight(qCeil(upperLimit()) + 2 * previewInset);

    refresh();

    layout()->activate();
    adjustSize();
    reposition();
    show();
    raise();
}


void UBPenWidthPopover::dismiss()
{
    mArmedButton = nullptr;

    if (!isHidden())
        hide();
}


void UBPenWidthPopover::showEvent(QShowEvent* event)
{
    qApp->installEventFilter(mOutsideFilter);
    UBFloatingPalette::showEvent(event);
}


void UBPenWidthPopover::hideEvent(QHideEvent* event)
{
    qApp->removeEventFilter(mOutsideFilter);
    UBFloatingPalette::hideEvent(event);
}


void UBPenWidthPopover::mousePressEvent(QMouseEvent* event)
{
    // anchored to its button: not dragged like the other palettes
    event->accept();
}


void UBPenWidthPopover::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}


void UBPenWidthPopover::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}


void UBPenWidthPopover::reposition()
{
    QWidget* container = parentWidget();

    if (!container || !mAnchor || !container->isAncestorOf(mAnchor))
        return;

    const int b = border();
    const int gap = 8;
    const QRect anchor(mAnchor->mapTo(container, QPoint(0, 0)), mAnchor->size());

    // below the glass body of the pen panel, or above it when the panel is at the bottom
    QRect panelBody = anchor;
    UBFloatingPalette* panel = qobject_cast<UBFloatingPalette*>(mPanel.data());

    if (panel)
    {
        const int pb = panel->border();
        panelBody = panel->geometry().adjusted(pb, pb, -pb, -pb);
    }

    const bool below = panelBody.center().y() < container->height() / 2;

    int x = anchor.center().x() - width() / 2;
    int y = below ? panelBody.bottom() + 1 + gap - b : panelBody.top() - gap - height() + b;

    // keep the body inside the board
    x = qBound(gap - b, x, qMax(gap - b, container->width() - width() + b - gap));
    y = qBound(-b, y, qMax(-b, container->height() - height() + b));

    if (QPoint(x, y) != pos())
        move(x, y);
}


void UBPenWidthPopover::refresh()
{
    const qreal width = presetWidth(mTool, mPreset);

    mUpdating = true;
    mSlider->setValue(sliderPosition(width));
    mUpdating = false;

    showWidth(width);
}


void UBPenWidthPopover::showWidth(qreal width)
{
    const int decimals = mTool == Eraser ? 0 : 1;
    mValue->setText(tr("%1 px").arg(QLocale().toString(width, 'f', decimals)));

    QColor color = Qt::black;
    const bool darkPage = UBSettings::settings()->isDarkBackground();

    if (UBApplication::boardController && mTool != Eraser)
    {
        UBBoardController* board = UBApplication::boardController;

        if (mTool == Marker)
            color = darkPage ? board->markerColorOnDarkBackground() : board->markerColorOnLightBackground();
        else
            color = darkPage ? board->penColorOnDarkBackground() : board->penColorOnLightBackground();
    }

    mPreview->setStroke(width, color, mTool == Eraser, darkPage);

    mResetButton->setEnabled(qAbs(width - defaultWidth()) > 0.5 / stepsPerPixel());
}


void UBPenWidthPopover::sliderChanged(int position)
{
    if (mUpdating)
        return;

    const qreal width = sliderWidth(position);

    if (!qFuzzyCompare(width, presetWidth(mTool, mPreset)))
        setPresetWidth(width);

    showWidth(width);
}


void UBPenWidthPopover::resetPreset()
{
    if (UBSetting* setting = presetSetting(mTool, mPreset))
    {
        mUpdating = true;
        setting->reset();
        mUpdating = false;
    }
    else
    {
        setPresetWidth(defaultWidth());
    }

    refresh();
}


void UBPenWidthPopover::updateIcons()
{
    const bool marker = UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker;

    auto update = [](QToolButton* button, qreal size, bool eraser) {
        QAction* action = button->defaultAction();

        // only when the glyph changes visibly, the actions are shared with other palettes
        if (!action || qAbs(action->property("ubWidthGlyph").toReal() - size) < 0.05)
            return;

        action->setProperty("ubWidthGlyph", size);
        action->setIcon(widthGlyph(size, eraser));
    };

    for (int preset = 0; preset < mLineButtons.size() && preset < 3; ++preset)
    {
        update(mLineButtons.at(preset), lineGlyphWidth(presetWidth(marker ? Marker : Pen, preset)), false);
    }

    for (int preset = 0; preset < mEraserButtons.size() && preset < 3; ++preset)
    {
        update(mEraserButtons.at(preset), eraserGlyphRadius(presetWidth(Eraser, preset)), true);
    }
}


UBSetting* UBPenWidthPopover::presetSetting(Tool tool, int preset)
{
    UBSettings* settings = UBSettings::settings();

    switch (tool)
    {
        case Pen:
        {
            UBSetting* pen[] = {settings->boardPenFineWidth, settings->boardPenMediumWidth, settings->boardPenStrongWidth};
            return pen[qBound(0, preset, 2)];
        }

        case Marker:
        {
            UBSetting* marker[] = {settings->boardMarkerFineWidth, settings->boardMarkerMediumWidth, settings->boardMarkerStrongWidth};
            return marker[qBound(0, preset, 2)];
        }

        default:
            // the eraser widths are plain values
            return nullptr;
    }
}


qreal UBPenWidthPopover::presetWidth(Tool tool, int preset)
{
    if (UBSetting* setting = presetSetting(tool, preset))
        return setting->get().toDouble();

    UBSettings* settings = UBSettings::settings();

    switch (preset)
    {
        case UBWidth::Fine:
            return settings->eraserFineWidth();
        case UBWidth::Medium:
            return settings->eraserMediumWidth();
        default:
            return settings->eraserStrongWidth();
    }
}


void UBPenWidthPopover::setPresetWidth(qreal width)
{
    if (UBSetting* setting = presetSetting(mTool, mPreset))
    {
        // the changed signal updates the glyphs
        mUpdating = true;
        setting->set(width);
        mUpdating = false;
        return;
    }

    UBSettings* settings = UBSettings::settings();

    switch (mPreset)
    {
        case UBWidth::Fine:
            settings->setEraserFineWidth(width);
            break;
        case UBWidth::Medium:
            settings->setEraserMediumWidth(width);
            break;
        default:
            settings->setEraserStrongWidth(width);
            break;
    }

    updateIcons();
}


qreal UBPenWidthPopover::defaultWidth() const
{
    if (UBSetting* setting = presetSetting(mTool, mPreset))
        return setting->defaultValue().toDouble();

    switch (mPreset)
    {
        case UBWidth::Fine:
            return UBSettings::defaultEraserFineWidth;
        case UBWidth::Medium:
            return UBSettings::defaultEraserMediumWidth;
        default:
            return UBSettings::defaultEraserStrongWidth;
    }
}


qreal UBPenWidthPopover::lowerLimit() const
{
    // the limits of the width sliders in Preferences
    return mTool == Eraser ? UBSettings::minEraserWidth : UBPreferencesController::sMinPenWidth;
}


qreal UBPenWidthPopover::upperLimit() const
{
    return mTool == Eraser ? UBSettings::maxEraserWidth : UBPreferencesController::sMaxPenWidth;
}


qreal UBPenWidthPopover::stepsPerPixel() const
{
    // Preferences steps the pen and marker widths by a tenth of a pixel
    return mTool == Eraser ? 1.0 : UBPreferencesController::sSliderRatio;
}


int UBPenWidthPopover::sliderPosition(qreal width) const
{
    const qreal range = upperLimit() - lowerLimit();
    const qreal t = range > 0 ? qBound(0.0, (width - lowerLimit()) / range, 1.0) : 0.0;
    return qRound(qSqrt(t) * sliderPositions);
}


qreal UBPenWidthPopover::sliderWidth(int position) const
{
    const qreal t = qBound(0, position, sliderPositions) / qreal(sliderPositions);
    const qreal width = lowerLimit() + (upperLimit() - lowerLimit()) * t * t;
    const qreal steps = stepsPerPixel();
    return qBound(lowerLimit(), qRound(width * steps) / steps, upperLimit());
}
