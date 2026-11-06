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

#include "UBLineAngleIndicator.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "gui/UBGlassStyle.h"

namespace
{
    // capsule metrics (logical pixels)
    constexpr int bodyHeight = 20;
    constexpr int horizontalPadding = 8;
    constexpr int shadowMargin = 6;
    // distance from the end point of the line: at least minDistance to the centre of the
    // capsule and at least gap to its nearest edge, so the pen tip and hand don't cover it
    constexpr qreal minDistance = 30;
    constexpr qreal gap = 16;
    // below this length (view pixels) the direction is not meaningful yet
    constexpr qreal minLength = 6;

    QFont labelFont(bool bold)
    {
        QFont font = QApplication::font();
        font.setPointSizeF(12);
        font.setWeight(bold ? QFont::DemiBold : QFont::Medium);
        return font;
    }
}

UBLineAngleIndicator::UBLineAngleIndicator(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

QString UBLineAngleIndicator::angleText(const QLineF& sceneLine, bool* exactAxis)
{
    const qreal dx = sceneLine.dx();
    const qreal dy = sceneLine.dy();
    const bool exact = (dx == 0.) != (dy == 0.);

    if (exactAxis)
        *exactAxis = exact;

    // QLineF::angle(): 0 = right, counter-clockwise positive on the screen, 0..360
    qreal angle = sceneLine.angle();

    if (angle > 180.)
        angle -= 360.;

    QString text;

    if (exact)
    {
        text = QString::number(qRound(angle));
    }
    else if (qRound(angle) % 90 == 0)
    {
        // close to, but not exactly on an axis: never pretend it is straight
        text = QString::number(angle, 'f', 1);

        if (text == QLatin1String("-0.0"))
            text = QStringLiteral("0.0");
    }
    else
    {
        text = QString::number(qRound(angle));
    }

    text.replace(QLatin1Char('-'), QChar(0x2212));
    text.append(QChar(0x00B0));
    return text;
}

void UBLineAngleIndicator::showLine(const QLineF& sceneLine, const QPointF& viewStart, const QPointF& viewEnd, bool onDarkBackground)
{
    const QLineF viewLine(viewStart, viewEnd);

    if (viewLine.length() < minLength || sceneLine.length() <= 0.)
    {
        hide();
        return;
    }

    bool exact = false;
    const QString text = angleText(sceneLine, &exact);

    if (text != mText || exact != mExactAxis || onDarkBackground != mOnDark)
    {
        mText = text;
        mExactAxis = exact;
        mOnDark = onDarkBackground;

        // the width only grows with the text, so the capsule does not jitter while drawing
        const QFontMetrics metrics(labelFont(true));
        const int textWidth = qMax(metrics.horizontalAdvance(mText), metrics.horizontalAdvance(QStringLiteral("00°")));
        const QSize size(textWidth + 2 * horizontalPadding + 2 * shadowMargin, bodyHeight + 2 * shadowMargin);

        if (size != this->size())
            resize(size);

        update();
    }

    // place the capsule beyond the end point, in the direction of the line
    const QPointF dir = (viewEnd - viewStart) / viewLine.length();
    const QRectF b = body();
    const qreal extent = qAbs(dir.x()) * b.width() / 2. + qAbs(dir.y()) * b.height() / 2.;
    QPointF centre = viewEnd + dir * qMax(minDistance, gap + extent);

    // keep it inside the view
    if (parentWidget())
    {
        const QRectF area = QRectF(parentWidget()->rect()).adjusted(b.width() / 2. + 4, b.height() / 2. + 4,
                                                                   -b.width() / 2. - 4, -b.height() / 2. - 4);

        if (area.isValid())
        {
            centre.setX(qBound(area.left(), centre.x(), area.right()));
            centre.setY(qBound(area.top(), centre.y(), area.bottom()));
        }
    }

    move((centre - QPointF(width() / 2., height() / 2.)).toPoint());

    if (isHidden())
    {
        show();
        raise();
    }
}

QRectF UBLineAngleIndicator::body() const
{
    return QRectF(rect()).adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin);
}

void UBLineAngleIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF b = body();
    const qreal radius = b.height() / 2.;
    const qreal dpr = devicePixelRatioF();

    // soft flat shadow so the capsule stays visible on a board of the same colour
    if (mShadow.isNull() || mShadowSize != size() || mShadowOnDark != mOnDark || !qFuzzyCompare(mShadow.devicePixelRatio(), dpr))
    {
        mShadow = UBGlassStyle::shadowImage(size(), b, radius, dpr, mOnDark);
        mShadowSize = size();
        mShadowOnDark = mOnDark;
    }

    painter.drawImage(QPointF(0, 0), mShadow);

    QColor textColor;

    if (mExactAxis)
    {
        // exactly horizontal or vertical: solid accent capsule, like an active control
        painter.setPen(Qt::NoPen);
        painter.setBrush(UBGlassStyle::accent());
        painter.drawPath(UBGlassStyle::roundedPath(b, radius));
        textColor = Qt::white;
    }
    else
    {
        UBGlassStyle::paintGlass(&painter, b, radius, UBGlassBackdrop(), mOnDark);
        textColor = UBGlassStyle::foreground(mOnDark);

        if (!mOnDark)
        {
            // the light glass on a white board: a thin neutral hairline keeps the edge visible
            painter.setPen(QPen(QColor(0, 0, 0, 30), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(UBGlassStyle::roundedPath(b.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5));
        }
    }

    painter.setFont(labelFont(mExactAxis));
    painter.setPen(textColor);
    painter.drawText(b, Qt::AlignCenter, mText);
}
