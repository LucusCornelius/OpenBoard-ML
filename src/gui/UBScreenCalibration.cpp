/*
 * Copyright (C) 2015-2025 Département de l'Instruction Publique (DIP-SEM)
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




#include "UBScreenCalibration.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QtMath>
#include <QPushButton>
#include <QScreen>

#include "board/UBBoardController.h"
#include "core/UBApplication.h"
#include "core/UBDisplayManager.h"
#include "core/UBSettings.h"


static constexpr const char* settingPixelsPerCm{"Board/ScreenPixelsPerCm"};
static constexpr double cmPerInch{2.54};
static constexpr double minPixelsPerCm{5.};
static constexpr double maxPixelsPerCm{500.};
static constexpr double calibrationLengthCm{10.};


double UBScreenCalibration::pixelsPerCm()
{
    const double calibrated = overridePixelsPerCm();
    return calibrated > 0. ? calibrated : detectedPixelsPerCm();
}

double UBScreenCalibration::detectedPixelsPerCm()
{
    QScreen* screen = UBApplication::displayManager ? UBApplication::displayManager->screen(ScreenRole::Control)
                                                    : nullptr;

    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }

    if (!screen)
    {
        return 96. / cmPerInch;
    }

    // physicalDotsPerInch() is computed from the device independent screen geometry and the
    // physical size reported by the display (EDID). Projectors and TVs often report nonsense,
    // so fall back to the logical DPI if the value is implausible.
    double dpi = screen->physicalDotsPerInch();

    if (!(dpi >= 50. && dpi <= 600.))
    {
        dpi = screen->logicalDotsPerInch();
    }

    return dpi / cmPerInch;
}

double UBScreenCalibration::overridePixelsPerCm()
{
    bool ok = false;
    const double value = UBSettings::settings()->value(settingPixelsPerCm, 0.).toDouble(&ok);
    return ok && value >= minPixelsPerCm && value <= maxPixelsPerCm ? value : 0.;
}

void UBScreenCalibration::setOverridePixelsPerCm(double pixelsPerCm)
{
    UBSettings::settings()->setValue(settingPixelsPerCm, pixelsPerCm > 0. ? pixelsPerCm : 0.);
}


/**
 * @brief Draws a line of calibrationLengthCm with centimetre ticks.
 */
class UBScreenCalibrationWidget::Ruler : public QWidget
{
public:
    explicit Ruler(QWidget* parent)
        : QWidget{parent}
    {
        setMinimumHeight(30);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setPixelsPerCm(double pixelsPerCm)
    {
        mPixelsPerCm = pixelsPerCm;
        setMinimumWidth(qCeil(calibrationLengthCm * pixelsPerCm) + 2 * margin);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen{palette().color(QPalette::WindowText)};
        pen.setWidthF(1.);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);

        const double y = height() - 6.;
        const double length = calibrationLengthCm * mPixelsPerCm;
        painter.drawLine(QPointF{margin, y}, QPointF{margin + length, y});

        for (int cm = 0; cm <= int(calibrationLengthCm); ++cm)
        {
            const double x = margin + cm * mPixelsPerCm;
            const double tick = (cm % 5 == 0) ? 20. : 10.;
            painter.drawLine(QPointF{x, y}, QPointF{x, y - tick});
        }
    }

private:
    static constexpr double margin{10.};
    double mPixelsPerCm{96. / cmPerInch};
};


UBScreenCalibrationWidget::UBScreenCalibrationWidget(QWidget* parent)
    : QGroupBox{parent}
{
    setObjectName("screenCalibrationGroupBox");
    setTitle(tr("Screen calibration (real size of grids and rulers)"));

    auto layout = new QGridLayout{this};

    auto explanation = new QLabel{tr("The line below should be exactly 10 cm long on the control screen. "
                                     "Measure it with a real ruler and enter the measured length if it differs.")};
    explanation->setWordWrap(true);
    layout->addWidget(explanation, 0, 0, 1, 4);

    mRuler = new Ruler{this};
    layout->addWidget(mRuler, 1, 0, 1, 4);

    layout->addWidget(new QLabel{tr("Measured length of the line:")}, 2, 0);

    mMeasuredSpinBox = new QDoubleSpinBox{this};
    mMeasuredSpinBox->setRange(1., 50.);
    mMeasuredSpinBox->setDecimals(1);
    mMeasuredSpinBox->setSingleStep(0.1);
    mMeasuredSpinBox->setSuffix(" cm");
    mMeasuredSpinBox->setValue(calibrationLengthCm);
    layout->addWidget(mMeasuredSpinBox, 2, 1);

    auto applyButton = new QPushButton{tr("Apply"), this};
    layout->addWidget(applyButton, 2, 2);

    layout->addWidget(new QLabel{tr("Pixels per centimetre:")}, 3, 0);

    mPixelsPerCmSpinBox = new QDoubleSpinBox{this};
    mPixelsPerCmSpinBox->setRange(minPixelsPerCm, maxPixelsPerCm);
    mPixelsPerCmSpinBox->setDecimals(2);
    mPixelsPerCmSpinBox->setSingleStep(0.1);
    mPixelsPerCmSpinBox->setSuffix(" px/cm");
    layout->addWidget(mPixelsPerCmSpinBox, 3, 1);

    auto automaticButton = new QPushButton{tr("Automatic"), this};
    automaticButton->setToolTip(tr("Use the physical size reported by the screen"));
    layout->addWidget(automaticButton, 3, 2);

    mInfoLabel = new QLabel{this};
    mInfoLabel->setWordWrap(true);
    layout->addWidget(mInfoLabel, 4, 0, 1, 4);

    layout->setColumnStretch(3, 1);

    connect(applyButton, &QPushButton::clicked, this, &UBScreenCalibrationWidget::applyMeasuredLength);
    connect(automaticButton, &QPushButton::clicked, this, &UBScreenCalibrationWidget::reset);
    connect(mPixelsPerCmSpinBox, &QDoubleSpinBox::editingFinished, this, [this]() {
        setPixelsPerCm(mPixelsPerCmSpinBox->value());
    });

    updateDisplay();
}

void UBScreenCalibrationWidget::reset()
{
    setPixelsPerCm(0.);
}

void UBScreenCalibrationWidget::applyMeasuredLength()
{
    const double measured = mMeasuredSpinBox->value();

    if (measured > 0.)
    {
        // the line was drawn with the current value, scale it accordingly
        setPixelsPerCm(UBScreenCalibration::pixelsPerCm() * calibrationLengthCm / measured);
    }
}

void UBScreenCalibrationWidget::setPixelsPerCm(double pixelsPerCm)
{
    if (pixelsPerCm > 0. && qAbs(pixelsPerCm - UBScreenCalibration::detectedPixelsPerCm()) < 0.005)
    {
        // same as detected value, no need for an override
        pixelsPerCm = 0.;
    }

    UBScreenCalibration::setOverridePixelsPerCm(pixelsPerCm);
    updateDisplay();

    if (UBApplication::boardController)
    {
        UBApplication::boardController->updateDefaultGridSize();
    }
}

void UBScreenCalibrationWidget::updateDisplay()
{
    const double pixelsPerCm = UBScreenCalibration::pixelsPerCm();
    const double detected = UBScreenCalibration::detectedPixelsPerCm();

    mRuler->setPixelsPerCm(pixelsPerCm);

    {
        const QSignalBlocker blocker{mPixelsPerCmSpinBox};
        mPixelsPerCmSpinBox->setValue(pixelsPerCm);
    }

    {
        const QSignalBlocker blocker{mMeasuredSpinBox};
        mMeasuredSpinBox->setValue(calibrationLengthCm);
    }

    QString info = tr("Detected by the system: %1 px/cm.").arg(detected, 0, 'f', 2);

    if (UBScreenCalibration::overridePixelsPerCm() > 0.)
    {
        info += " " + tr("Using your calibration: %1 px/cm.").arg(pixelsPerCm, 0, 'f', 2);
    }

    info += " " + tr("A new calibration applies to new and empty pages and to \"Reset grid size\"; "
                     "it is exact at 100% zoom.");
    mInfoLabel->setText(info);
}
