/*
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

#include "UBColorPickerPopup.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/UBApplication.h"
#include "core/UBColorPalette.h"
#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "board/UBDrawingController.h"
#include "gui/UBColorPaletteDialog.h"
#include "gui/UBMainWindow.h"

#include "core/memcheck.h"

namespace
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    typedef float ColorComponent;

    QPointF eventPosition(QMouseEvent* event)
    {
        return event->position();
    }
#else
    typedef qreal ColorComponent;

    QPointF eventPosition(QMouseEvent* event)
    {
        return event->localPos();
    }
#endif

    QList<QObject*> associatedObjectsOf(QAction* action)
    {
        QList<QObject*> objects;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        objects = action->associatedObjects();
#else
        for (QWidget* widget : action->associatedWidgets())
        {
            objects << widget;
        }
#endif

        return objects;
    }

    QWidget* anchorForAction(QAction* action)
    {
        if (!action)
            return nullptr;

        QWidget* firstVisible = nullptr;

        for (QObject* object : associatedObjectsOf(action))
        {
            QToolButton* button = qobject_cast<QToolButton*>(object);

            if (!button || !button->isVisible())
                continue;

            if (button->rect().contains(button->mapFromGlobal(QCursor::pos())))
                return button;

            if (!firstVisible)
                firstVisible = button;
        }

        return firstVisible;
    }

    void drawCheckerboard(QPainter& painter, const QRectF& rect)
    {
        painter.save();
        painter.setClipRect(rect);
        painter.fillRect(rect, Qt::white);

        const int cell = 6;

        for (int y = 0; y * cell < rect.height(); ++y)
        {
            for (int x = 0; x * cell < rect.width(); ++x)
            {
                if ((x + y) % 2)
                {
                    painter.fillRect(QRectF(rect.left() + x * cell, rect.top() + y * cell, cell, cell), QColor(204, 204, 204));
                }
            }
        }

        painter.restore();
    }

    bool isMarkerTool()
    {
        return UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker;
    }

    qreal markerAlpha()
    {
        return UBSettings::settings()->boardMarkerAlpha->get().toDouble();
    }
}


// ---------------------------------------------------------------------------
// UBHueSaturationArea
// ---------------------------------------------------------------------------

UBHueSaturationArea::UBHueSaturationArea(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
}


QSize UBHueSaturationArea::sizeHint() const
{
    return QSize(280, 170);
}


QSize UBHueSaturationArea::minimumSizeHint() const
{
    return QSize(160, 100);
}


void UBHueSaturationArea::setHueSaturation(qreal hue, qreal saturation)
{
    mHue = qBound<qreal>(0, hue, 1);
    mSaturation = qBound<qreal>(0, saturation, 1);
    update();
}


void UBHueSaturationArea::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const int w = qMax(1, width());
    const int h = qMax(1, height());

    mImage = QImage(w, h, QImage::Format_RGB32);

    for (int y = 0; y < h; ++y)
    {
        const qreal saturation = h > 1 ? 1.0 - qreal(y) / (h - 1) : 1.0;
        QRgb* line = reinterpret_cast<QRgb*>(mImage.scanLine(y));

        for (int x = 0; x < w; ++x)
        {
            const qreal hue = w > 1 ? qreal(x) / (w - 1) : 0.0;
            line[x] = QColor::fromHsvF(qMin<qreal>(hue, 1.0), saturation, 1.0).rgb();
        }
    }
}


void UBHueSaturationArea::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    if (mImage.size() != size())
    {
        // can happen before the first resize event
        painter.fillRect(rect(), palette().window());
    }
    else
    {
        painter.drawImage(0, 0, mImage);
    }

    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF center(mHue * (width() - 1), (1.0 - mSaturation) * (height() - 1));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawEllipse(center, 8, 8);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawEllipse(center, 8, 8);
}


void UBHueSaturationArea::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        pick(eventPosition(event));
    }
}


void UBHueSaturationArea::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        pick(eventPosition(event));
    }
}


void UBHueSaturationArea::pick(const QPointF& pos)
{
    const qreal hue = width() > 1 ? qBound<qreal>(0, pos.x() / (width() - 1), 1) : 0;
    const qreal saturation = height() > 1 ? qBound<qreal>(0, 1.0 - pos.y() / (height() - 1), 1) : 1;

    setHueSaturation(hue, saturation);
    emit hueSaturationChanged(mHue, mSaturation);
}


// ---------------------------------------------------------------------------
// UBColorValueBar
// ---------------------------------------------------------------------------

UBColorValueBar::UBColorValueBar(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setCursor(Qt::SizeVerCursor);
}


QSize UBColorValueBar::sizeHint() const
{
    return QSize(36, 170);
}


QSize UBColorValueBar::minimumSizeHint() const
{
    return QSize(28, 100);
}


void UBColorValueBar::setColor(qreal hue, qreal saturation, qreal value)
{
    mHue = qBound<qreal>(0, hue, 1);
    mSaturation = qBound<qreal>(0, saturation, 1);
    mValue = qBound<qreal>(0, value, 1);
    update();
}


void UBColorValueBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF bar = QRectF(rect()).adjusted(4, 0, -4, 0);

    QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
    gradient.setColorAt(0, QColor::fromHsvF(mHue, mSaturation, 1.0));
    gradient.setColorAt(1, Qt::black);

    painter.setPen(QPen(palette().mid().color(), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(bar, 4, 4);

    const qreal y = (1.0 - mValue) * (height() - 1);
    const QRectF handle(0.5, qBound<qreal>(0.5, y - 4, height() - 8.5), width() - 1, 8);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawRoundedRect(handle, 3, 3);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawRoundedRect(handle, 3, 3);
}


void UBColorValueBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        pick(eventPosition(event));
    }
}


void UBColorValueBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        pick(eventPosition(event));
    }
}


void UBColorValueBar::pick(const QPointF& pos)
{
    mValue = height() > 1 ? qBound<qreal>(0, 1.0 - pos.y() / (height() - 1), 1) : 1;
    update();
    emit valueChanged(mValue);
}


// ---------------------------------------------------------------------------
// UBColorSwatchRow
// ---------------------------------------------------------------------------

UBColorSwatchRow::UBColorSwatchRow(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}


void UBColorSwatchRow::setColors(const QList<QColor>& colors)
{
    mColors = colors;
    updateGeometry();
    update();
}


QSize UBColorSwatchRow::sizeHint() const
{
    return QSize(qMax(1, int(mColors.size())) * 22, 28);
}


QSize UBColorSwatchRow::minimumSizeHint() const
{
    return QSize(qMax(1, int(mColors.size())) * 14, 28);
}


QRectF UBColorSwatchRow::cellRect(int index) const
{
    if (mColors.isEmpty())
        return QRectF();

    const qreal cellWidth = qreal(width()) / mColors.size();
    return QRectF(index * cellWidth, 0, cellWidth, height()).adjusted(1.5, 1.5, -1.5, -1.5);
}


void UBColorSwatchRow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().mid().color(), 1));

    for (int i = 0; i < mColors.size(); ++i)
    {
        painter.setBrush(mColors.at(i));
        painter.drawRoundedRect(cellRect(i), 3, 3);
    }
}


void UBColorSwatchRow::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF pos = eventPosition(event);

    for (int i = 0; i < mColors.size(); ++i)
    {
        if (cellRect(i).adjusted(-1.5, -1.5, 1.5, 1.5).contains(pos))
        {
            emit colorClicked(mColors.at(i));
            return;
        }
    }
}


// ---------------------------------------------------------------------------
// UBColorPreview
// ---------------------------------------------------------------------------

UBColorPreview::UBColorPreview(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(sizeHint());
}


QSize UBColorPreview::sizeHint() const
{
    return QSize(72, 36);
}


void UBColorPreview::setColors(const QColor& oldColor, const QColor& newColor)
{
    mOldColor = oldColor;
    mNewColor = newColor;
    update();
}


void UBColorPreview::setBackground(const QColor& background)
{
    mBackground = background;
    update();
}


void UBColorPreview::setAlpha(qreal alpha)
{
    mAlpha = qBound<qreal>(0, alpha, 1);
    update();
}


void UBColorPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QPainterPath clip;
    clip.addRoundedRect(area, 5, 5);
    painter.setClipPath(clip);

    painter.fillRect(area, mBackground);

    // draw a stroke-like band of each color so the result looks like it does on the board
    const qreal half = area.width() / 2;
    QColor oldColor = mOldColor;
    QColor newColor = mNewColor;
    oldColor.setAlphaF(mAlpha);
    newColor.setAlphaF(mAlpha);

    painter.fillRect(QRectF(area.left() + 4, area.top() + 6, half - 4, area.height() - 12), oldColor);
    painter.fillRect(QRectF(area.left() + half, area.top() + 6, half - 4, area.height() - 12), newColor);

    painter.setClipping(false);
    painter.setPen(QPen(palette().mid().color(), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(area, 5, 5);
}


// ---------------------------------------------------------------------------
// UBColorPickerWidget
// ---------------------------------------------------------------------------

UBColorPickerWidget::UBColorPickerWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QHBoxLayout* spectrumLayout = new QHBoxLayout();
    spectrumLayout->setSpacing(8);
    mArea = new UBHueSaturationArea(this);
    mArea->setToolTip(tr("Hue and saturation"));
    mValueBar = new UBColorValueBar(this);
    mValueBar->setToolTip(tr("Brightness"));
    spectrumLayout->addWidget(mArea, 1);
    spectrumLayout->addWidget(mValueBar);
    layout->addLayout(spectrumLayout, 1);

    mQuickColors = new UBColorSwatchRow(this);
    QList<QColor> quickColors;

    for (int i = 0; i < 12; ++i)
    {
        quickColors << QColor::fromHsvF(i / 12.0, 1.0, 1.0);
    }

    quickColors << QColor(Qt::white) << QColor(128, 128, 128) << QColor(Qt::black);
    mQuickColors->setColors(quickColors);
    layout->addWidget(mQuickColors);

    QHBoxLayout* valueLayout = new QHBoxLayout();
    valueLayout->setSpacing(6);

    mPreview = new UBColorPreview(this);
    mPreview->setToolTip(tr("Previous and new color"));
    valueLayout->addWidget(mPreview);

    mHexEdit = new QLineEdit(this);
    mHexEdit->setMaxLength(20);
    mHexEdit->setMinimumWidth(80);
    mHexEdit->setMinimumHeight(30);
    mHexEdit->setToolTip(tr("Hexadecimal color, e.g. #FF8800"));
    valueLayout->addWidget(mHexEdit, 1);

    auto createSpinBox = [this](const QString& toolTip, const QString& prefix)
    {
        QSpinBox* spinBox = new QSpinBox(this);
        spinBox->setRange(0, 255);
        spinBox->setPrefix(prefix);
        spinBox->setToolTip(toolTip);
        spinBox->setMinimumHeight(30);
        spinBox->setAccelerated(true);
        connect(spinBox, SIGNAL(valueChanged(int)), this, SLOT(rgbEdited()));
        return spinBox;
    };

    mRed = createSpinBox(tr("Red"), tr("R "));
    mGreen = createSpinBox(tr("Green"), tr("G "));
    mBlue = createSpinBox(tr("Blue"), tr("B "));
    valueLayout->addWidget(mRed);
    valueLayout->addWidget(mGreen);
    valueLayout->addWidget(mBlue);
    layout->addLayout(valueLayout);

    connect(mArea, &UBHueSaturationArea::hueSaturationChanged, this, &UBColorPickerWidget::hueSaturationPicked);
    connect(mValueBar, &UBColorValueBar::valueChanged, this, &UBColorPickerWidget::valuePicked);
    connect(mQuickColors, &UBColorSwatchRow::colorClicked, this, [this](const QColor& color) {
        setColor(color);
        emit colorChanged(this->color());
    });
    connect(mHexEdit, &QLineEdit::editingFinished, this, &UBColorPickerWidget::hexEdited);
    connect(mHexEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        // apply complete values while typing
        static const QRegularExpression complete(QStringLiteral("^#?[0-9A-Fa-f]{6}$"));
        if (complete.match(text.trimmed()).hasMatch())
            hexEdited();
    });

    setColor(Qt::black);
}


QColor UBColorPickerWidget::color() const
{
    return QColor::fromHsvF(mHue, mSaturation, mValue);
}


void UBColorPickerWidget::setColor(const QColor& color)
{
    if (!color.isValid())
        return;

    ColorComponent h, s, v;
    color.toHsv().getHsvF(&h, &s, &v);

    // keep hue and saturation for achromatic colors so that the user can
    // continue from where he was when moving the brightness bar
    if (h >= 0 && s > 0)
        mHue = h;

    if (v > 0)
        mSaturation = s;

    mValue = v;

    if (!mInitialColor.isValid())
        mInitialColor = this->color();

    updateControls();
}


void UBColorPickerWidget::setInitialColor(const QColor& color)
{
    mInitialColor = color;
    setColor(color);
}


void UBColorPickerWidget::setPreviewBackground(const QColor& background)
{
    mPreview->setBackground(background);
}


void UBColorPickerWidget::setPreviewAlpha(qreal alpha)
{
    mPreview->setAlpha(alpha);
}


void UBColorPickerWidget::hueSaturationPicked(qreal hue, qreal saturation)
{
    mHue = hue;
    mSaturation = saturation;

    // picking in the spectrum of a black color would not show anything
    if (mValue < 0.05)
        mValue = 1.0;

    updateControls();
    emit colorChanged(color());
}


void UBColorPickerWidget::valuePicked(qreal value)
{
    mValue = value;
    updateControls();
    emit colorChanged(color());
}


void UBColorPickerWidget::hexEdited()
{
    if (mUpdating)
        return;

    QString text = mHexEdit->text().trimmed();

    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f]{3}([0-9A-Fa-f]{3})?$"));
    static const QRegularExpression rgbTriple(QStringLiteral("^(\\d{1,3})\\s*[,; ]\\s*(\\d{1,3})\\s*[,; ]\\s*(\\d{1,3})$"));

    QColor parsed;

    const QRegularExpressionMatch rgbMatch = rgbTriple.match(text);

    if (rgbMatch.hasMatch())
    {
        parsed = QColor(qMin(255, rgbMatch.captured(1).toInt()),
                        qMin(255, rgbMatch.captured(2).toInt()),
                        qMin(255, rgbMatch.captured(3).toInt()));
    }
    else
    {
        if (hexOnly.match(text).hasMatch())
            text.prepend(QLatin1Char('#'));

#if (QT_VERSION >= QT_VERSION_CHECK(6, 4, 0))
        parsed = QColor::fromString(text);
#else
        parsed.setNamedColor(text);
#endif
    }

    if (parsed.isValid())
    {
        parsed.setAlpha(255);

        if (parsed.rgb() != color().rgb())
        {
            setColor(parsed);
            emit colorChanged(color());
        }
    }
    else
    {
        updateControls(); // restore the current value
    }
}


void UBColorPickerWidget::rgbEdited()
{
    if (mUpdating)
        return;

    const QColor edited(mRed->value(), mGreen->value(), mBlue->value());

    if (edited.rgb() != color().rgb())
    {
        setColor(edited);
        emit colorChanged(color());
    }
}


void UBColorPickerWidget::updateControls()
{
    mUpdating = true;

    const QColor current = color();

    mArea->setHueSaturation(mHue, mSaturation);
    mValueBar->setColor(mHue, mSaturation, mValue);
    mPreview->setColors(mInitialColor.isValid() ? mInitialColor : current, current);

    if (!mHexEdit->hasFocus())
    {
        mHexEdit->setText(current.name(QColor::HexRgb).toUpper());
    }

    {
        const QSignalBlocker redBlocker(mRed);
        const QSignalBlocker greenBlocker(mGreen);
        const QSignalBlocker blueBlocker(mBlue);
        mRed->setValue(current.red());
        mGreen->setValue(current.green());
        mBlue->setValue(current.blue());
    }

    mUpdating = false;
}


// ---------------------------------------------------------------------------
// UBColorPickerDialog
// ---------------------------------------------------------------------------

UBColorPickerDialog::UBColorPickerDialog(const QColor& initial, QWidget* parent, const QString& title)
    : QDialog(parent)
{
    setWindowTitle(title.isEmpty() ? tr("Select Color") : title);
    setObjectName(QStringLiteral("colorPickerDialog"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    mPicker = new UBColorPickerWidget(this);
    mPicker->setInitialColor(UBColorPalette::opaque(initial));
    layout->addWidget(mPicker, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* more = buttons->addButton(tr("More..."), QDialogButtonBox::ResetRole);
    more->setToolTip(tr("Open the system color dialog"));
    connect(more, &QPushButton::clicked, this, &UBColorPickerDialog::moreColors);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}


QColor UBColorPickerDialog::color() const
{
    return mPicker->color();
}


void UBColorPickerDialog::moreColors()
{
    const QColor chosen = QColorDialog::getColor(mPicker->color(), this, windowTitle());

    if (chosen.isValid())
    {
        mPicker->setColor(UBColorPalette::opaque(chosen));
    }
}


QColor UBColorPickerDialog::getColor(const QColor& initial, QWidget* parent, const QString& title,
                                     const QColor& background, qreal alpha)
{
    UBColorPickerDialog dialog(initial, parent, title);
    dialog.picker()->setPreviewBackground(background);
    dialog.picker()->setPreviewAlpha(alpha);

    if (dialog.exec() == QDialog::Accepted)
    {
        return dialog.color();
    }

    return QColor();
}


// ---------------------------------------------------------------------------
// UBColorPickerPopup
// ---------------------------------------------------------------------------

QPointer<UBColorPickerPopup> UBColorPickerPopup::sInstance;


UBColorPickerPopup::UBColorPickerPopup(int swatchIndex, QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , mSwatchIndex(swatchIndex)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("colorPickerPopup"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);

    UBSettings* settings = UBSettings::settings();
    UBColorPaletteManager* manager = UBColorPaletteManager::manager();

    const bool marker = isMarkerTool();
    const bool dark = settings->isDarkBackground();

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    mTitle = new QLabel(this);
    QFont titleFont = mTitle->font();
    titleFont.setBold(true);
    mTitle->setFont(titleFont);

    QString title = marker ? tr("Marker color") : tr("Pen color");
    title += dark ? tr(" (dark background)") : tr(" (light background)");

    if (mSwatchIndex >= 0)
    {
        title += QStringLiteral(" - ") + tr("color %1").arg(mSwatchIndex + 1);
    }

    mTitle->setText(title);
    layout->addWidget(mTitle);

    // palette selector
    QHBoxLayout* paletteLayout = new QHBoxLayout();
    QLabel* paletteLabel = new QLabel(tr("Palette"), this);
    mPaletteCombo = new QComboBox(this);
    mPaletteCombo->setMinimumHeight(32);
    mPaletteCombo->setToolTip(tr("Active color palette"));
    QPushButton* manageButton = new QPushButton(tr("Manage..."), this);
    manageButton->setMinimumHeight(32);
    manageButton->setToolTip(tr("Create, rename, edit and delete color palettes"));
    paletteLayout->addWidget(paletteLabel);
    paletteLayout->addWidget(mPaletteCombo, 1);
    paletteLayout->addWidget(manageButton);
    layout->addLayout(paletteLayout);

    // picker
    mPicker = new UBColorPickerWidget(this);
    mPicker->setPreviewBackground(dark ? QColor(Qt::black) : QColor(Qt::white));
    mPicker->setPreviewAlpha(marker ? markerAlpha() : 1.0);

    QColor initial = manager->currentToolColor();

    if (mSwatchIndex >= 0)
    {
        const QList<QColor> colors = marker ? settings->markerColors(dark) : settings->penColors(dark);
        initial = UBColorPalette::opaque(colors.value(mSwatchIndex, initial));
    }

    mPicker->setInitialColor(initial);
    layout->addWidget(mPicker, 1);

    // actions
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(6);

    QPushButton* moreButton = new QPushButton(tr("More..."), this);
    moreButton->setToolTip(tr("Open the system color dialog"));
    moreButton->setMinimumHeight(36);
    buttonLayout->addWidget(moreButton);
    buttonLayout->addStretch(1);

    if (mSwatchIndex >= 0)
    {
        QPushButton* swatchButton = new QPushButton(tr("Set color %1").arg(mSwatchIndex + 1), this);
        swatchButton->setToolTip(tr("Replace this color of the active palette"));
        swatchButton->setMinimumHeight(36);
        buttonLayout->addWidget(swatchButton);
        connect(swatchButton, &QPushButton::clicked, this, &UBColorPickerPopup::setSwatch);
    }

    QPushButton* addButton = new QPushButton(tr("Add to palette"), this);
    addButton->setToolTip(tr("Add this color to the active palette"));
    addButton->setMinimumHeight(36);
    buttonLayout->addWidget(addButton);

    QPushButton* useButton = new QPushButton(tr("Use"), this);
    useButton->setToolTip(tr("Draw with this color now"));
    useButton->setMinimumHeight(36);
    useButton->setDefault(true);
    buttonLayout->addWidget(useButton);

    layout->addLayout(buttonLayout);

    connect(moreButton, &QPushButton::clicked, this, &UBColorPickerPopup::moreColors);
    connect(addButton, &QPushButton::clicked, this, &UBColorPickerPopup::addToPalette);
    connect(useButton, &QPushButton::clicked, this, &UBColorPickerPopup::useColor);
    connect(manageButton, &QPushButton::clicked, this, &UBColorPickerPopup::managePalettes);
    connect(mPaletteCombo, SIGNAL(activated(int)), this, SLOT(paletteActivated(int)));
    connect(manager, &UBColorPaletteManager::palettesChanged, this, &UBColorPickerPopup::refreshPalettes);
    connect(manager, &UBColorPaletteManager::activePaletteChanged, this, &UBColorPickerPopup::refreshPalettes);

    refreshPalettes();
}


void UBColorPickerPopup::showPopup(QWidget* anchor, int swatchIndex)
{
    if (sInstance)
    {
        sInstance->close();
    }

    QWidget* parent = anchor ? anchor->window() : dialogParent();
    UBColorPickerPopup* popup = new UBColorPickerPopup(swatchIndex, parent);
    sInstance = popup;

    popup->placeNear(anchor);
    popup->show();
    popup->raise();
    popup->activateWindow();
}


void UBColorPickerPopup::setupIntegration(UBMainWindow* mainWindow, const QList<QAction*>& colorActions)
{
    UBColorPaletteManager* manager = UBColorPaletteManager::manager();
    manager->connectSignals();

    QObject::connect(mainWindow->actionCustomColor, &QAction::triggered, mainWindow, [mainWindow]() {
        showPopup(anchorForAction(mainWindow->actionCustomColor));
    });

    QObject::connect(mainWindow->actionNextColorPalette, &QAction::triggered, manager, &UBColorPaletteManager::selectNextPalette);
    QObject::connect(mainWindow->actionPreviousColorPalette, &QAction::triggered, manager, &UBColorPaletteManager::selectPreviousPalette);

    for (QAction* action : colorActions)
    {
        // selecting a palette color ends the use of a custom color
        if (action && action->isCheckable())
        {
            QObject::connect(action, &QAction::triggered, manager, &UBColorPaletteManager::clearCustomColor);
        }
    }

    installSwatchHandlers(colorActions);
}


void UBColorPickerPopup::installSwatchHandlers(const QList<QAction*>& colorActions)
{
    UBColorSwatchEventFilter* filter = UBColorSwatchEventFilter::instance();
    filter->addColorActions(colorActions);

    for (QAction* action : colorActions)
    {
        if (!action || !action->isCheckable())
            continue;

        for (QObject* object : associatedObjectsOf(action))
        {
            if (QToolButton* button = qobject_cast<QToolButton*>(object))
            {
                filter->watch(button);
            }
        }
    }
}


QWidget* UBColorPickerPopup::dialogParent()
{
    QWidget* active = QApplication::activeWindow();

    if (active && active->isVisible())
        return active;

    return UBApplication::mainWindow;
}


void UBColorPickerPopup::placeNear(QWidget* anchor)
{
    adjustSize();
    const QSize popupSize = size();

    QScreen* screen = anchor ? anchor->screen() : QGuiApplication::screenAt(QCursor::pos());

    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    QPoint pos;

    if (anchor)
    {
        const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
        pos = QPoint(anchorRect.left(), anchorRect.bottom() + 4);

        if (pos.y() + popupSize.height() > available.bottom())
        {
            // not enough room below the anchor (e.g. toolbar at the bottom)
            pos.setY(anchorRect.top() - popupSize.height() - 4);
        }
    }
    else
    {
        pos = QCursor::pos() - QPoint(popupSize.width() / 2, popupSize.height() / 2);
    }

    pos.setX(qBound(available.left(), pos.x(), qMax(available.left(), available.right() - popupSize.width())));
    pos.setY(qBound(available.top(), pos.y(), qMax(available.top(), available.bottom() - popupSize.height())));

    move(pos);
}


void UBColorPickerPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        useColor();
        return;
    }

    QFrame::keyPressEvent(event);
}


void UBColorPickerPopup::useColor()
{
    const QColor color = mPicker->color();
    close();
    UBColorPaletteManager::manager()->useColor(color);
}


void UBColorPickerPopup::addToPalette()
{
    const QColor color = mPicker->color();
    close();
    UBColorPaletteManager::manager()->addColorToActivePalette(color);
}


void UBColorPickerPopup::setSwatch()
{
    const QColor color = mPicker->color();
    const int index = mSwatchIndex;
    close();
    UBColorPaletteManager::manager()->setActivePaletteSlot(index, color);
}


void UBColorPickerPopup::moreColors()
{
    // a popup cannot stay open while a modal dialog is shown
    const QPoint position = pos();
    hide();

    const QColor chosen = QColorDialog::getColor(mPicker->color(), dialogParent(), tr("Select Color"));

    if (chosen.isValid())
    {
        mPicker->setColor(UBColorPalette::opaque(chosen));
    }

    move(position);
    show();
    raise();
    activateWindow();
}


void UBColorPickerPopup::managePalettes()
{
    close();
    UBColorPaletteDialog::showManager(dialogParent());
}


void UBColorPickerPopup::paletteActivated(int index)
{
    UBColorPaletteManager* manager = UBColorPaletteManager::manager();

    if (index != manager->activeIndex())
    {
        manager->setActivePalette(index);
    }
}


void UBColorPickerPopup::refreshPalettes()
{
    UBColorPaletteManager* manager = UBColorPaletteManager::manager();

    const QSignalBlocker blocker(mPaletteCombo);
    mPaletteCombo->clear();
    mPaletteCombo->addItems(manager->paletteNames());
    mPaletteCombo->setCurrentIndex(manager->activeIndex());
}


// ---------------------------------------------------------------------------
// UBColorSwatchEventFilter
// ---------------------------------------------------------------------------

UBColorSwatchEventFilter* UBColorSwatchEventFilter::instance()
{
    static QPointer<UBColorSwatchEventFilter> sFilter;

    if (!sFilter)
    {
        sFilter = new UBColorSwatchEventFilter(qApp);
    }

    return sFilter;
}


UBColorSwatchEventFilter::UBColorSwatchEventFilter(QObject* parent)
    : QObject(parent)
{
    mTimer = new QTimer(this);
    mTimer->setSingleShot(true);
    mTimer->setInterval(600);
    connect(mTimer, &QTimer::timeout, this, &UBColorSwatchEventFilter::longPressTimeout);
}


void UBColorSwatchEventFilter::addColorActions(const QList<QAction*>& actions)
{
    for (QAction* action : actions)
    {
        if (action && action->isCheckable() && !mColorActions.contains(action))
        {
            mColorActions << action;
        }
    }
}


void UBColorSwatchEventFilter::watch(QToolButton* button)
{
    if (!button || button->property("ubColorSwatchWatched").toBool())
        return;

    button->setProperty("ubColorSwatchWatched", true);
    button->installEventFilter(this);
}


int UBColorSwatchEventFilter::swatchIndex(QToolButton* button) const
{
    if (!button || !button->defaultAction() || !button->defaultAction()->isEnabled())
        return -1;

    return mColorActions.indexOf(button->defaultAction());
}


bool UBColorSwatchEventFilter::eventFilter(QObject* watched, QEvent* event)
{
    QToolButton* button = qobject_cast<QToolButton*>(watched);

    if (!button)
        return QObject::eventFilter(watched, event);

    switch (event->type())
    {
        case QEvent::ContextMenu:
        {
            const int index = swatchIndex(button);

            if (index >= 0)
            {
                mTimer->stop();
                UBColorPickerPopup::showPopup(button, index);
                return true;
            }

            break;
        }

        case QEvent::MouseButtonPress:
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

            if (mouseEvent->button() == Qt::LeftButton && swatchIndex(button) >= 0)
            {
                mPressedButton = button;
                mLongPressed = false;
                mTimer->start();
            }

            break;
        }

        case QEvent::MouseMove:
        {
            if (mTimer->isActive() && mPressedButton == button)
            {
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

                if (!button->rect().contains(eventPosition(mouseEvent).toPoint()))
                {
                    mTimer->stop();
                }
            }

            break;
        }

        case QEvent::MouseButtonRelease:
        {
            mTimer->stop();

            if (mLongPressed && mPressedButton == button)
            {
                // the popup was already opened, do not select the swatch
                mLongPressed = false;
                return true;
            }

            break;
        }

        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}


void UBColorSwatchEventFilter::longPressTimeout()
{
    QToolButton* button = mPressedButton;

    if (!button || !button->isDown())
        return;

    const int index = swatchIndex(button);

    if (index < 0)
        return;

    mLongPressed = true;

    // release the button without triggering it
    button->setDown(false);

    UBColorPickerPopup::showPopup(button, index);
}
