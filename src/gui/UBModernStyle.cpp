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

#include "UBModernStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRegularExpression>

#include "gui/UBGlassStyle.h"
#include "gui/UBUiStyle.h"

#include "core/memcheck.h"

namespace
{
    QString rgba(const QColor& c)
    {
        return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }

    QColor withAlpha(QColor c, qreal alpha)
    {
        c.setAlphaF(alpha);
        return c;
    }
}


QColor UBModernStyle::surface(bool dark)
{
    return dark ? QColor(0x2c, 0x2c, 0x2c) : QColor(0xf9, 0xf9, 0xf9);
}


QColor UBModernStyle::stroke(bool dark)
{
    return dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 30);
}


QColor UBModernStyle::foreground(bool dark)
{
    return dark ? QColor(255, 255, 255) : QColor(0x1a, 0x1a, 0x1a);
}


QColor UBModernStyle::accent(bool dark)
{
    // the default accent of Windows 11 (blue) when the system has none
    QColor base = UBUiStyle::systemAccent();

    if (!base.isValid() || base.alpha() == 0)
        base = QColor(0x00, 0x5f, 0xb8);

    // like the Fluent accent fills: AccentDark1 on light, AccentLight2 on dark
    float h, s, l, a;
    base.getHslF(&h, &s, &l, &a);

    if (dark)
        l = qMax(l, 0.68f);
    else
        l = qMin(l, 0.40f);

    return QColor::fromHslF(h, s, l, 1.0f);
}


QColor UBModernStyle::checkedForeground(bool dark)
{
    return accent(dark);
}


void UBModernStyle::paintPanel(QPainter* painter, const QRectF& body, qreal radius, bool dark)
{
    if (body.isEmpty())
        return;

    const qreal r = qMax<qreal>(0.0, qMin(radius, qMin(body.width(), body.height()) / 2.0));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->fillPath(UBGlassStyle::roundedPath(body, r), surface(dark));

    // 1 px stroke on the pixel grid
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(stroke(dark), 1.0));
    painter->drawPath(UBGlassStyle::roundedPath(body.adjusted(0.5, 0.5, -0.5, -0.5), qMax(0.0, r - 0.5)));
    painter->restore();
}


QImage UBModernStyle::shadowImage(const QSize& outerSize, const QRectF& body, qreal radius, qreal dpr, bool dark)
{
    QImage image(outerSize * dpr, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    if (body.isEmpty())
        return image;

    const qreal margin = qMin(qMin(body.left(), body.top()),
                              qMin(outerSize.width() - body.right(), outerSize.height() - body.bottom()));

    if (margin >= 4)
    {
        // Fluent flyout elevation: a soft shadow a few pixels below the surface,
        // fading out inside the margin of the widget
        const qreal offset = qMin<qreal>(3.0, margin * 0.25);
        const qreal blurRadius = qMin<qreal>(10.0, (margin - offset) * 0.7);

        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.fillPath(UBGlassStyle::roundedPath(body.translated(0, offset).adjusted(1, 1, -1, 0), radius),
                   QColor(0, 0, 0, dark ? 110 : 46));
        p.end();

        UBGlassStyle::blur(image, qMax(1, qRound(blurRadius * dpr)));
    }

    // ambient contact shadow along the edge
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(dpr, dpr);
    p.fillPath(UBGlassStyle::roundedPath(body.adjusted(-0.5, -0.5, 0.5, 1.0), radius + 0.5), QColor(0, 0, 0, dark ? 70 : 14));
    p.end();

    image.setDevicePixelRatio(dpr);
    return image;
}


void UBModernStyle::adjustPalette(QPalette& palette, bool dark)
{
    const QColor accentColor = accent(dark);

    if (dark)
    {
        palette.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(0x2c, 0x2c, 0x2c));
        palette.setColor(QPalette::AlternateBase, QColor(0x32, 0x32, 0x32));
        palette.setColor(QPalette::ToolTipBase, QColor(0x2c, 0x2c, 0x2c));
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(0x2d, 0x2d, 0x2d));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::Light, QColor(0x45, 0x45, 0x45));
        palette.setColor(QPalette::Midlight, QColor(0x38, 0x38, 0x38));
        palette.setColor(QPalette::Mid, QColor(0x2a, 0x2a, 0x2a));
        palette.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::Shadow, QColor(0x00, 0x00, 0x00));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x78, 0x78, 0x78));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x78, 0x78, 0x78));
        palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0x27, 0x27, 0x27));
        palette.setColor(QPalette::Disabled, QPalette::Button, QColor(0x27, 0x27, 0x27));
    }
    else
    {
        palette.setColor(QPalette::Window, QColor(0xf3, 0xf3, 0xf3));
        palette.setColor(QPalette::WindowText, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(0xf9, 0xf9, 0xf9));
        palette.setColor(QPalette::ToolTipBase, QColor(0xf9, 0xf9, 0xf9));
        palette.setColor(QPalette::ToolTipText, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::Text, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::Button, QColor(0xfb, 0xfb, 0xfb));
        palette.setColor(QPalette::ButtonText, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::Light, Qt::white);
        palette.setColor(QPalette::Midlight, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::Mid, QColor(0xe0, 0xe0, 0xe0));
        palette.setColor(QPalette::Dark, QColor(0xc4, 0xc4, 0xc4));
        palette.setColor(QPalette::Shadow, QColor(0x9e, 0x9e, 0x9e));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xa0, 0xa0, 0xa0));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0xa0, 0xa0, 0xa0));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa0, 0xa0, 0xa0));
        palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0xf5, 0xf5, 0xf5));
        palette.setColor(QPalette::Disabled, QPalette::Button, QColor(0xf5, 0xf5, 0xf5));
    }

    palette.setColor(QPalette::Highlight, accentColor);
    palette.setColor(QPalette::Link, accentColor);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, accentColor);
#endif
}


QString UBModernStyle::neutralizeThemeStyleSheet(const QString& css)
{
    static const QRegularExpression hexColor(QStringLiteral("#([0-9a-fA-F]{6})\\b"));

    // fills with white text in both themes: the accent at the lightness of the light theme
    const QColor accentBase = accent(false);
    const float accentLightness = accentBase.lightnessF();

    QString result;
    result.reserve(css.size());
    qsizetype last = 0;

    QRegularExpressionMatchIterator it = hexColor.globalMatch(css);

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const QColor color(QStringLiteral("#") + match.captured(1));

        result += css.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();

        const int hue = color.hsvHue();
        const float saturation = color.hsvSaturationF();
        const bool slate = hue >= 190 && hue <= 240;
        QColor mapped = color;

        if (slate && saturation >= 0.5f && color.valueF() >= 0.5f)
        {
            // a blue of the theme: the accent with the same relative lightness
            const float lightness = qBound(0.0f, accentLightness + (color.lightnessF() - 0.52f), 1.0f);
            mapped = QColor::fromHslF(accentBase.hslHueF(), accentBase.hslSaturationF(), lightness);
        }
        else if (slate || saturation < 0.06f)
        {
            // a blue tinted grey: neutral grey of the same luminance
            const int luma = qRound(0.2126 * color.red() + 0.7152 * color.green() + 0.0722 * color.blue());
            mapped = QColor(luma, luma, luma);
        }

        result += mapped.name();
    }

    result += css.mid(last);
    return result;
}


QString UBModernStyle::styleSheet(bool dark)
{
    const QColor fg = foreground(dark);
    const QColor fgSecondary = withAlpha(fg, dark ? 0.79 : 0.62);
    const QColor fgDisabled = withAlpha(fg, 0.36);
    const QColor accentColor = accent(dark);
    const QColor window = dark ? QColor(0x20, 0x20, 0x20) : QColor(0xf3, 0xf3, 0xf3);
    const QColor card = surface(dark);
    const QColor hover = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14);
    const QColor pressed = dark ? QColor(255, 255, 255, 11) : QColor(0, 0, 0, 24);
    const QColor checked = withAlpha(accentColor, dark ? 0.26 : 0.13);
    const QColor checkedHover = withAlpha(accentColor, dark ? 0.32 : 0.18);
    const QColor hairline = dark ? QColor(255, 255, 255, 21) : QColor(0, 0, 0, 22);
    const QColor controlFill = dark ? QColor(255, 255, 255, 15) : QColor(255, 255, 255, 179);
    const QColor controlFillHover = dark ? QColor(255, 255, 255, 21) : QColor(249, 249, 249, 128);
    const QColor controlStroke = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 15);
    const QColor controlStrokeBottom = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 41);
    const QColor fieldBottom = dark ? QColor(255, 255, 255, 138) : QColor(0, 0, 0, 115);
    const QColor track = dark ? QColor(255, 255, 255, 139) : QColor(0, 0, 0, 114);
    const QColor segment = dark ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 10);
    const QColor segmentOn = dark ? QColor(255, 255, 255, 22) : QColor(255, 255, 255);
    const QColor onAccent = dark ? QColor(0, 0, 0) : QColor(255, 255, 255);
    const QColor accentHover = withAlpha(accentColor, 0.90);

    QString css = QStringLiteral(R"CSS(
/* ---------- Modern layer (UBModernStyle, Windows 11 / Fluent like) ---------- */

QWidget
{
    background-color: @WINDOW@;
    color: @FG@;
}

QMainWindow,
QDialog,
QMessageBox,
QMenuBar,
QStatusBar,
QToolBar,
QDockWidget::title
{
    background-color: @WINDOW@;
    color: @FG@;
}

QPushButton
{
    background-color: @CONTROL@;
    color: @FG@;
    border: 1px solid @CONTROLSTROKE@;
    border-bottom: 1px solid @CONTROLSTROKEBOTTOM@;
    border-radius: 4px;
    padding: 5px 14px;
    min-height: 20px;
}

QPushButton:hover
{
    background-color: @CONTROLHOVER@;
}

QPushButton:pressed
{
    background-color: @CONTROLHOVER@;
    color: @FGSECONDARY@;
    border-bottom: 1px solid @CONTROLSTROKE@;
}

QPushButton:checked,
QPushButton:default
{
    background-color: @ACCENT@;
    color: @ONACCENT@;
    border: 1px solid @ACCENT@;
}

QPushButton:checked:hover,
QPushButton:default:hover
{
    background-color: @ACCENTHOVER@;
}

QPushButton:disabled
{
    background-color: @CONTROL@;
    color: @FGDISABLED@;
    border: 1px solid @CONTROLSTROKE@;
}

QDialog#preferencesDialog QPushButton#closeButton,
QDialog#preferencesDialog QPushButton#closeButton:hover,
QDialog#preferencesDialog QPushButton#closeButton:pressed,
QDialog#preferencesDialog QPushButton#closeButton:default
{
    color: @ONACCENT@;
}

QToolButton
{
    background-color: transparent;
    color: @FG@;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 5px;
    margin: 2px;
}

QToolButton:hover
{
    background-color: @HOVER@;
    border: 1px solid transparent;
}

QToolButton:pressed
{
    background-color: @PRESSED@;
    color: @FGSECONDARY@;
}

QToolButton:checked
{
    background-color: @CHECKED@;
    color: @ACCENT@;
    border: 1px solid transparent;
}

QToolTip
{
    background-color: @CARD@;
    color: @FG@;
    border: 1px solid @HAIR@;
    padding: 4px 8px;
}

QToolBar#boardToolBar,
QToolBar#webToolBar,
QToolBar#documentToolBar
{
    background: @WINDOW@;
    border: none;
    border-bottom: 1px solid @HAIR@;
    spacing: 2px;
    padding: 4px 8px;
}

QToolBar#boardToolBar::separator,
QToolBar#webToolBar::separator,
QToolBar#documentToolBar::separator
{
    background: @HAIR@;
    border: none;
    width: 1px;
    margin: 8px 6px;
}

QToolBar#boardToolBar QToolButton,
QToolBar#webToolBar QToolButton,
QToolBar#documentToolBar QToolButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 4px 6px;
    margin: 0px;
    min-width: 30px;
    min-height: 30px;
    height: 28px;
}

QToolBar#boardToolBar[glassText="true"] QToolButton,
QToolBar#webToolBar[glassText="true"] QToolButton,
QToolBar#documentToolBar[glassText="true"] QToolButton
{
    height: 44px;
}

QToolBar#boardToolBar QToolButton:hover,
QToolBar#webToolBar QToolButton:hover,
QToolBar#documentToolBar QToolButton:hover
{
    background: @HOVER@;
    border: none;
}

QToolBar#boardToolBar QToolButton:pressed,
QToolBar#webToolBar QToolButton:pressed,
QToolBar#documentToolBar QToolButton:pressed
{
    background: @PRESSED@;
    color: @FGSECONDARY@;
}

QToolBar#boardToolBar QToolButton:checked,
QToolBar#webToolBar QToolButton:checked,
QToolBar#documentToolBar QToolButton:checked
{
    background: @CHECKED@;
    color: @ACCENT@;
}

QToolBar#boardToolBar QToolButton:disabled,
QToolBar#webToolBar QToolButton:disabled,
QToolBar#documentToolBar QToolButton:disabled
{
    background: transparent;
    color: @FGDISABLED@;
}

QToolBar QToolButton#ubButtonMenu
{
    padding-right: 14px;
}

QToolBar QToolButton#ubButtonMenu::menu-indicator
{
    subcontrol-origin: padding;
    subcontrol-position: bottom right;
    right: 3px;
    bottom: 6px;
}

/* floating palettes paint their own card */
#DesktopPropertyPalette,
#AddItemPalette,
#BackgroundPalette,
#MathPalette
{
    background: transparent;
    border: none;
}

UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupLeft,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupCenter,
UBToolbarButtonGroup QToolButton#desktop-ubButtonGroupRight
{
    qproperty-iconSize: 20px 20px;
}

/* ---------- popups, menus, dialogs ---------- */

QFrame#colorPickerPopup
{
    background: @CARD@;
    border: 1px solid @HAIR@;
    border-radius: 0px;
}

QFrame#colorPickerPopup QLabel
{
    background: transparent;
}

QMenu
{
    background-color: @CARD@;
    border: 1px solid @HAIR@;
    padding: 4px 0px;
}

QMenu::item
{
    background-color: transparent;
    color: @FG@;
    padding: 7px 28px 7px 12px;
    margin: 1px 4px;
    border-radius: 4px;
}

QMenu::item:selected
{
    background-color: @HOVER@;
    color: @FG@;
}

QMenu::item:disabled
{
    color: @FGDISABLED@;
}

QMenu::icon
{
    padding-left: 8px;
}

QMenu::separator
{
    height: 1px;
    background: @HAIR@;
    border: none;
    margin: 4px 0px;
}

QDialog#preferencesDialog QTabBar::tab
{
    background: transparent;
    color: @FGSECONDARY@;
    border: none;
    border-radius: 4px;
    padding: 6px 14px;
    margin: 6px 2px 4px 2px;
    min-width: 8ex;
}

QDialog#preferencesDialog QTabBar::tab:hover:!selected
{
    background: @HOVER@;
    color: @FG@;
}

QDialog#preferencesDialog QTabBar::tab:selected
{
    background: @CHECKED@;
    color: @FG@;
    border: none;
}

QDialog#preferencesDialog QTabWidget::pane
{
    border: 1px solid @HAIR@;
    border-radius: 8px;
    top: -1px;
}
)CSS");

    css.replace("@WINDOW@", rgba(window));
    css.replace("@CARD@", rgba(card));
    css.replace("@HAIR@", rgba(hairline));
    css.replace("@FGSECONDARY@", rgba(fgSecondary));
    css.replace("@FGDISABLED@", rgba(fgDisabled));
    css.replace("@FG@", rgba(fg));
    css.replace("@HOVER@", rgba(hover));
    css.replace("@PRESSED@", rgba(pressed));
    css.replace("@CHECKED@", rgba(checked));
    css.replace("@CONTROLHOVER@", rgba(controlFillHover));
    css.replace("@CONTROLSTROKEBOTTOM@", rgba(controlStrokeBottom));
    css.replace("@CONTROLSTROKE@", rgba(controlStroke));
    css.replace("@CONTROL@", rgba(controlFill));
    css.replace("@ACCENTHOVER@", rgba(accentHover));
    css.replace("@ACCENT@", rgba(accentColor));
    css.replace("@ONACCENT@", rgba(onAccent));

    // floating palettes: the same selectors as the glass layer (property "glassOnDark",
    // which follows the theme in this style), so the specificity is the same
    static const QString panelTemplate = QStringLiteral(R"CSS(
/* ---------- floating palettes (modern) ---------- */

@P@ UBActionPaletteButton,
@P@ QToolButton#glassButton,
@P@ QToolButton#clusterButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 4px;
    margin: 0px;
    font-weight: normal;
}

@P@ UBActionPaletteButton#glassToggle,
@P@ UBActionPaletteButton#glassToggle:checked,
@P@ UBActionPaletteButton#glassToggle:hover
{
    background: transparent;
}

@P@ QToolButton#clusterButton
{
    min-width: 40px;
    min-height: 40px;
}

@P@[glassLabels="true"] QToolButton#clusterButton
{
    border-radius: 4px;
    padding: 4px 4px 2px 4px;
    min-width: 54px;
    font-size: 11px;
}

@P@ QToolButton#clusterButton::menu-indicator
{
    image: none;
    width: 0px;
}

@P@ UBActionPaletteButton:hover,
@P@ QToolButton#glassButton:hover,
@P@ QToolButton#clusterButton:hover
{
    background: @HOVER@;
}

@P@ UBActionPaletteButton:pressed,
@P@ QToolButton#glassButton:pressed,
@P@ QToolButton#clusterButton:pressed
{
    background: @PRESSED@;
    color: @FGSECONDARY@;
}

@P@ UBActionPaletteButton:checked,
@P@ QToolButton#glassButton:checked,
@P@ QToolButton#clusterButton:checked
{
    background: @CHECKED@;
    color: @ACCENT@;
}

@P@ UBActionPaletteButton:checked:hover,
@P@ QToolButton#glassButton:checked:hover,
@P@ QToolButton#clusterButton:checked:hover
{
    background: @CHECKEDHOVER@;
}

@P@ UBActionPaletteButton:disabled,
@P@ QToolButton#glassButton:disabled,
@P@ QToolButton#clusterButton:disabled
{
    color: @FGDISABLED@;
    background: transparent;
}

@P@ QFrame#clusterSeparator
{
    background: @HAIR@;
    border: none;
    min-width: 1px;
    max-width: 1px;
    margin-top: 10px;
    margin-bottom: 10px;
}

/* segmented control (maths palette tabs) */
@P@ QFrame#glassSegmented
{
    background: @SEGMENT@;
    border: 1px solid @HAIR@;
    border-radius: 6px;
}

@P@ QToolButton#glassSegment
{
    color: @FGSECONDARY@;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px 12px;
    margin: 0px;
    min-height: 18px;
}

@P@ QToolButton#glassSegment:hover
{
    background: @HOVER@;
    color: @FG@;
}

@P@ QToolButton#glassSegment:checked
{
    background: @SEGMENTON@;
    border: 1px solid @HAIR@;
    color: @FG@;
    font-weight: 600;
}

@P@ QStackedWidget,
@P@ QStackedWidget > QWidget,
@P@ QWidget#shapeToolWidget,
@P@ QWidget#penColorSection,
@P@ QSlider,
@P@ QAbstractScrollArea,
@P@ QAbstractScrollArea > QWidget#qt_scrollarea_viewport,
@P@ QAbstractScrollArea > QWidget#qt_scrollarea_viewport > QWidget,
@P@ #BackgroundPaletteScrollArea,
@P@ #BackgroundPaletteScrollArea QWidget
{
    background: transparent;
    border: none;
}

@P@ QSlider::groove:horizontal
{
    height: 4px;
    margin: 0px;
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

@P@ QSlider::sub-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @ACCENT@;
}

@P@ QSlider::add-page:horizontal
{
    border: none;
    border-radius: 2px;
    background: @TRACK@;
}

@P@ QSlider::handle:horizontal
{
    width: 20px;
    height: 20px;
    margin: -8px 0px;
    border-radius: 10px;
    border: 1px solid @HAIR@;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
                                stop:0 @ACCENT@, stop:0.52 @ACCENT@, stop:0.6 @THUMB@, stop:1 @THUMB@);
}

@P@ QLineEdit
{
    color: @FG@;
    background: @FIELD@;
    border: 1px solid @CONTROLSTROKE@;
    border-bottom: 1px solid @FIELDBOTTOM@;
    border-radius: 4px;
    padding: 4px 10px;
    min-height: 20px;
    selection-background-color: @ACCENT@;
    selection-color: @ONACCENT@;
}

@P@ QLineEdit:focus
{
    background: @FIELDFOCUS@;
    border-bottom: 2px solid @ACCENT@;
}

@P@ QLabel,
@P@ #BackgroundPalette QLabel,
@P@ #BackgroundPaletteLabel,
@P@ #UBStartupHintsPalette QCheckBox
{
    color: @FG@;
    background: transparent;
}

@P@ QLabel#glassHint
{
    color: @FGSECONDARY@;
    font-size: 11px;
}

@P@ QCheckBox
{
    color: @FG@;
    background: transparent;
}

@P@ QToolButton#closeButton,
@P@#BackgroundPalette QToolButton#closeButton,
@P@#AddItemPalette QToolButton#closeButton,
@P@#UBStartupHintsPalette QToolButton#closeButton
{
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 0px;
    margin: 0px;
    min-width: 22px;
    max-width: 22px;
    min-height: 22px;
    max-height: 22px;
}

@P@ QToolButton#closeButton:hover,
@P@#BackgroundPalette QToolButton#closeButton:hover,
@P@#AddItemPalette QToolButton#closeButton:hover,
@P@#UBStartupHintsPalette QToolButton#closeButton:hover
{
    background: @HOVER@;
}

/* pen panel and desktop property palettes */

@P@ UBToolbarButtonGroup QToolButton,
@P@ QToolButton#glass-shapeFill
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 0px;
    margin: 0px;
    min-width: 36px;
    max-width: 36px;
    min-height: 36px;
    max-height: 36px;
}

@P@ UBToolbarButtonGroup QToolButton:hover,
@P@ QToolButton#glass-shapeFill:hover
{
    background: @HOVER@;
}

@P@ UBToolbarButtonGroup QToolButton:pressed,
@P@ QToolButton#glass-shapeFill:pressed
{
    background: @PRESSED@;
}

@P@ UBToolbarButtonGroup QToolButton:checked,
@P@ QToolButton#glass-shapeFill:checked
{
    background: @CHECKED@;
    color: @ACCENT@;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]
{
    border: 2px solid transparent;
    border-radius: 18px;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]:checked
{
    background: transparent;
    border: 2px solid @ACCENT@;
}

@P@ UBToolbarButtonGroup QToolButton[colorSwatch="true"]:checked:hover
{
    background: @HOVER@;
}

@P@ UBToolbarButtonGroup QToolButton[colorPaletteConfigButton="true"]
{
    background: transparent;
    border: none;
}

@P@ UBToolbarButtonGroup QToolButton[colorPaletteConfigButton="true"]:hover
{
    background: @HOVER@;
}

@P@ QToolButton#paletteSwitchButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 0px;
    margin: 0px;
    min-width: 20px;
    max-width: 20px;
    min-height: 36px;
    max-height: 36px;
}

@P@ QToolButton#paletteSwitchButton:hover
{
    background: @HOVER@;
}

@P@ QToolButton#paletteSwitchButton:pressed
{
    background: @PRESSED@;
}

@P@ QFrame#glassDivider
{
    background: @HAIR@;
    border: none;
    min-width: 1px;
    max-width: 1px;
    margin-top: 9px;
    margin-bottom: 9px;
}

@P@ QPushButton#zoomPaletteButton
{
    color: @FG@;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 18px;
    font-weight: 600;
}

@P@ QPushButton#zoomPaletteButton:hover
{
    background: @HOVER@;
}

)CSS");

    // the property always follows the theme in the modern style; both blocks use the
    // colours of the theme so that a palette is right even before it re-polished
    for (const bool onDark : {false, true})
    {
        QString block = panelTemplate;
        block.replace("@P@", QString("UBFloatingPalette[glassOnDark=\"%1\"]").arg(onDark ? "true" : "false"));
        block.replace("@FGSECONDARY@", rgba(fgSecondary));
        block.replace("@FGDISABLED@", rgba(fgDisabled));
        block.replace("@FG@", rgba(fg));
        block.replace("@HOVER@", rgba(hover));
        block.replace("@PRESSED@", rgba(pressed));
        block.replace("@CHECKEDHOVER@", rgba(checkedHover));
        block.replace("@CHECKED@", rgba(checked));
        block.replace("@HAIR@", rgba(hairline));
        block.replace("@TRACK@", rgba(track));
        block.replace("@THUMB@", rgba(dark ? QColor(0x45, 0x45, 0x45) : QColor(255, 255, 255)));
        block.replace("@FIELDFOCUS@", rgba(dark ? QColor(0x1f, 0x1f, 0x1f) : QColor(255, 255, 255)));
        block.replace("@FIELDBOTTOM@", rgba(fieldBottom));
        block.replace("@FIELD@", rgba(controlFill));
        block.replace("@CONTROLSTROKE@", rgba(controlStroke));
        block.replace("@SEGMENTON@", rgba(segmentOn));
        block.replace("@SEGMENT@", rgba(segment));
        block.replace("@ONACCENT@", rgba(onAccent));
        block.replace("@ACCENT@", rgba(accentColor));
        css += block;
    }

    // document mode: shared with the glass layer, in the Fluent colours and radii
    css += UBGlassStyle::documentModeStyleSheet(dark);

    return css;
}
