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

#pragma once

#include <QImage>
#include <QLineF>
#include <QWidget>

/**
 * Live angle readout while a straight line is drawn (Line tool, Shapes tool
 * line and arrow): a small flat glass capsule next to the end point of the
 * line, on the side away from the start point.
 *
 * It is a child widget of the board view, so it is never part of the scene:
 * not saved, not in thumbnails, PDF export or the projection view.
 *
 * Angle convention: 0° points to the right, counter-clockwise positive, range
 * −179..180 (up = 90°, left = 180°, down = −90°). When the line is exactly
 * horizontal or vertical in scene coordinates the capsule shows the accent state.
 */
class UBLineAngleIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit UBLineAngleIndicator(QWidget* parent);

    /**
     * Show the angle of sceneLine. viewStart and viewEnd are the end points of the
     * line in the coordinates of the parent widget. onDarkBackground selects the
     * dark material (light text).
     */
    void showLine(const QLineF& sceneLine, const QPointF& viewStart, const QPointF& viewEnd, bool onDarkBackground);

    /** text shown for a line, also used by tests */
    static QString angleText(const QLineF& sceneLine, bool* exactAxis = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF body() const;

    QString mText;
    bool mExactAxis{false};
    bool mOnDark{false};
    QImage mShadow;
    QSize mShadowSize;
    bool mShadowOnDark{false};
};
