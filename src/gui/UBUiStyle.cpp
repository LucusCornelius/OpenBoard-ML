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

#include "UBUiStyle.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#endif
#endif

#include "core/memcheck.h"

namespace
{
    QColor sSystemAccent;

#ifdef Q_OS_WIN
    // DWMWINDOWATTRIBUTE values, not declared by older Windows SDKs
    constexpr DWORD ubDwmUseImmersiveDarkModeBefore20H1 = 19;
    constexpr DWORD ubDwmUseImmersiveDarkMode = 20;
    constexpr DWORD ubDwmWindowCornerPreference = 33;
    // DWM_WINDOW_CORNER_PREFERENCE::DWMWCP_ROUND
    constexpr int ubDwmCornerRound = 2;

    bool sChromeDark = false;

    /** dark or light title bar and rounded corners for a top level window with a frame */
    void applyChromeTo(QWidget* widget)
    {
        if (!widget || !widget->isWindow())
            return;

        const Qt::WindowType type = widget->windowType();

        if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen || type == Qt::Desktop
                || widget->windowFlags().testFlag(Qt::FramelessWindowHint))
        {
            return;
        }

        const WId id = widget->internalWinId();

        if (!id)
            return;

        HWND hwnd = reinterpret_cast<HWND>(id);

        const BOOL dark = sChromeDark ? TRUE : FALSE;

        if (FAILED(DwmSetWindowAttribute(hwnd, ubDwmUseImmersiveDarkMode, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, ubDwmUseImmersiveDarkModeBefore20H1, &dark, sizeof(dark));

        // Windows 11 only, fails harmlessly on Windows 10
        const int corner = ubDwmCornerRound;
        DwmSetWindowAttribute(hwnd, ubDwmWindowCornerPreference, &corner, sizeof(corner));

        // repaint the frame of a window that is already shown
        if (widget->isVisible())
        {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    /** gives the windows shown later (dialogs, preferences) the chrome of the theme */
    class UBWindowChromeFilter : public QObject
    {
        public:
            using QObject::QObject;

            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (event->type() == QEvent::Show && watched->isWidgetType())
                {
                    QWidget* widget = static_cast<QWidget*>(watched);

                    if (widget->isWindow())
                        applyChromeTo(widget);
                }

                return false;
            }
    };
#endif
}


UBUiStyle::Style UBUiStyle::style()
{
    static const Style sStyle = []() {
        const QString requested = qEnvironmentVariable("OPENBOARD_UI_STYLE").trimmed().toLower();

        if (requested == "glass")
            return Glass;

        if (requested == "modern")
            return Modern;

#ifdef Q_OS_MACOS
        // Liquid Glass is the look of macOS
        return Glass;
#else
        // painted glass imitates Apple: Windows and Linux get the modern (Fluent like) look
        return Modern;
#endif
    }();

    return sStyle;
}


QColor UBUiStyle::systemAccent()
{
    return sSystemAccent;
}


void UBUiStyle::applyApplicationStyle()
{
    // the theme replaces the application palette: remember the accent of the system first
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    sSystemAccent = QApplication::palette().color(QPalette::Accent);
#else
    sSystemAccent = QApplication::palette().color(QPalette::Highlight);
#endif

    QString key = qEnvironmentVariable("OPENBOARD_QT_STYLE").trimmed();

    if (key.isEmpty())
    {
        key = QStringLiteral("fusion");

#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // Windows 11 controls for dialogs and preferences; from Qt 6.8 the colour
        // scheme of the style can follow the application theme (UBThemeManager)
        if (isModern() && QStyleFactory::keys().contains(QStringLiteral("windows11"), Qt::CaseInsensitive))
            key = QStringLiteral("windows11");
#endif
    }

    if (!QApplication::setStyle(key))
        QApplication::setStyle(QStringLiteral("fusion"));

#ifdef Q_OS_WIN
    if (isModern())
    {
        // the variable version of Segoe UI of Windows 11, Segoe UI (the default) elsewhere
        for (const QString& family : {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI Variable")})
        {
            if (QFontDatabase::hasFamily(family))
            {
                QFont font = QApplication::font();
                font.setFamily(family);
                QApplication::setFont(font);
                break;
            }
        }
    }
#endif

    qDebug() << "User interface style:" << (isGlass() ? "glass" : "modern") << "- Qt style:" << QApplication::style()->name();
}


void UBUiStyle::applyWindowChrome(bool dark)
{
#ifdef Q_OS_WIN
    static bool filterInstalled = false;

    sChromeDark = dark;

    if (!filterInstalled && qApp)
    {
        filterInstalled = true;
        qApp->installEventFilter(new UBWindowChromeFilter(qApp));
    }

    const QWidgetList windows = QApplication::topLevelWidgets();

    for (QWidget* window : windows)
        applyChromeTo(window);
#else
    Q_UNUSED(dark);
#endif
}
