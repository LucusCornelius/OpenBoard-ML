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




#pragma once

#include <QGroupBox>

// forward
class QDoubleSpinBox;
class QLabel;

/**
 * @brief Physical size of the control screen, used to show background grids in real centimetres.
 *
 * All values are in logical (device independent) pixels, i.e. the same unit as widget
 * coordinates and the transformation of the board view.
 */
namespace UBScreenCalibration
{
    //! Effective logical pixels per centimetre on the control screen (user override or detected value).
    double pixelsPerCm();

    //! Logical pixels per centimetre as reported by the control screen (QScreen::physicalDotsPerInch).
    double detectedPixelsPerCm();

    //! User calibration override, 0 if the detected value is used.
    double overridePixelsPerCm();

    //! Set the user calibration override, 0 to use the detected value.
    void setOverridePixelsPerCm(double pixelsPerCm);
}


/**
 * @brief Preferences widget to calibrate the screen size.
 *
 * Shows a line which should measure 10 cm on screen. The user can enter the measured
 * length or directly the number of pixels per centimetre.
 */
class UBScreenCalibrationWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit UBScreenCalibrationWidget(QWidget* parent = nullptr);

public slots:
    void reset();

private:
    void applyMeasuredLength();
    void setPixelsPerCm(double pixelsPerCm);
    void updateDisplay();

private:
    class Ruler;

    Ruler* mRuler{nullptr};
    QLabel* mInfoLabel{nullptr};
    QDoubleSpinBox* mMeasuredSpinBox{nullptr};
    QDoubleSpinBox* mPixelsPerCmSpinBox{nullptr};
};
