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

#include "UBColorPalette.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtMath>
#include <QtGlobal>

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "board/UBBoardController.h"
#include "board/UBDrawingController.h"

#include "core/memcheck.h"

namespace
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    typedef float ColorComponent;
#else
    typedef qreal ColorComponent;
#endif

    qreal linearize(qreal c)
    {
        return c <= 0.04045 ? c / 12.92 : qPow((c + 0.055) / 1.055, 2.4);
    }

    // relative luminance (WCAG definition), 0 = black, 1 = white
    qreal luminance(const QColor& color)
    {
        return 0.2126 * linearize(color.redF())
             + 0.7152 * linearize(color.greenF())
             + 0.0722 * linearize(color.blueF());
    }

    QColor colorFromString(const QString& name)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 4, 0))
        return QColor::fromString(name);
#else
        QColor color;
        color.setNamedColor(name);
        return color;
#endif
    }

    QJsonArray toJsonArray(const QList<QColor>& colors)
    {
        QJsonArray array;

        for (const QColor& color : colors)
        {
            array.append(UBColorPalette::opaque(color).name(QColor::HexRgb));
        }

        return array;
    }

    QList<QColor> fromJsonArray(const QJsonValue& value)
    {
        QList<QColor> colors;

        for (const QJsonValue& entry : value.toArray())
        {
            QColor color = colorFromString(entry.toString());

            if (color.isValid())
            {
                colors << UBColorPalette::opaque(color);
            }
        }

        return colors;
    }

    QList<QColor> opaqueList(const QList<QColor>& colors)
    {
        QList<QColor> result;

        for (const QColor& color : colors)
        {
            result << UBColorPalette::opaque(color);
        }

        return result;
    }

    QList<QColor> padded(QList<QColor> colors, const QList<QColor>& filler, int size)
    {
        if (colors.size() > size)
        {
            colors = colors.mid(0, size);
        }

        for (int i = colors.size(); i < size; ++i)
        {
            colors << (filler.isEmpty() ? QColor(Qt::black) : filler.at(i % filler.size()));
        }

        return colors;
    }

    bool sameList(const QList<QColor>& a, const QList<QColor>& b)
    {
        if (a.size() != b.size())
            return false;

        for (int i = 0; i < a.size(); ++i)
        {
            if (a.at(i).rgb() != b.at(i).rgb())
                return false;
        }

        return true;
    }
}


// ---------------------------------------------------------------------------
// UBColorPalette
// ---------------------------------------------------------------------------

UBColorPalette::UBColorPalette(const QString& name)
    : name(name)
{
}


int UBColorPalette::minSize()
{
    return UBSettings::minColorPaletteSize;
}


int UBColorPalette::maxSize()
{
    return UBSettings::maxColorPaletteSize;
}


int UBColorPalette::size() const
{
    return penLight.size();
}


void UBColorPalette::normalize(const UBColorPalette& filler)
{
    int count = qMax(qMax(penLight.size(), penDark.size()), qMax(markerLight.size(), markerDark.size()));
    resize(qBound(minSize(), count, maxSize()), filler);
}


void UBColorPalette::resize(int size, const UBColorPalette& filler)
{
    size = qBound(minSize(), size, maxSize());

    penLight = padded(opaqueList(penLight), filler.penLight, size);
    penDark = padded(opaqueList(penDark), filler.penDark, size);
    markerLight = padded(opaqueList(markerLight), filler.markerLight, size);
    markerDark = padded(opaqueList(markerDark), filler.markerDark, size);
}


void UBColorPalette::appendColor(const QColor& light, const QColor& dark)
{
    appendSlot(light, dark, light, dark);
}


void UBColorPalette::appendSlot(const QColor& penLightColor, const QColor& penDarkColor,
                                const QColor& markerLightColor, const QColor& markerDarkColor)
{
    penLight << opaque(penLightColor);
    penDark << opaque(penDarkColor);
    markerLight << opaque(markerLightColor);
    markerDark << opaque(markerDarkColor);
}


void UBColorPalette::setSlot(int index, bool marker, const QColor& light, const QColor& dark)
{
    if (index < 0 || index >= size())
        return;

    if (marker)
    {
        markerLight[index] = opaque(light);
        markerDark[index] = opaque(dark);
    }
    else
    {
        penLight[index] = opaque(light);
        penDark[index] = opaque(dark);
    }
}


void UBColorPalette::removeSlot(int index)
{
    if (index < 0 || index >= size())
        return;

    penLight.removeAt(index);
    penDark.removeAt(index);
    markerLight.removeAt(index);
    markerDark.removeAt(index);
}


void UBColorPalette::moveSlot(int from, int to)
{
    if (from < 0 || from >= size() || to < 0 || to >= size() || from == to)
        return;

    penLight.move(from, to);
    penDark.move(from, to);
    markerLight.move(from, to);
    markerDark.move(from, to);
}


bool UBColorPalette::sameColors(const UBColorPalette& other) const
{
    return sameList(penLight, other.penLight)
        && sameList(penDark, other.penDark)
        && sameList(markerLight, other.markerLight)
        && sameList(markerDark, other.markerDark);
}


QJsonObject UBColorPalette::toJson() const
{
    QJsonObject pen;
    pen.insert(QStringLiteral("light"), toJsonArray(penLight));
    pen.insert(QStringLiteral("dark"), toJsonArray(penDark));

    QJsonObject marker;
    marker.insert(QStringLiteral("light"), toJsonArray(markerLight));
    marker.insert(QStringLiteral("dark"), toJsonArray(markerDark));

    QJsonObject json;
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("pen"), pen);
    json.insert(QStringLiteral("marker"), marker);
    return json;
}


UBColorPalette UBColorPalette::fromJson(const QJsonObject& json)
{
    UBColorPalette palette(json.value(QStringLiteral("name")).toString());

    const QJsonObject pen = json.value(QStringLiteral("pen")).toObject();
    const QJsonObject marker = json.value(QStringLiteral("marker")).toObject();

    palette.penLight = fromJsonArray(pen.value(QStringLiteral("light")));
    palette.penDark = fromJsonArray(pen.value(QStringLiteral("dark")));
    palette.markerLight = fromJsonArray(marker.value(QStringLiteral("light")));
    palette.markerDark = fromJsonArray(marker.value(QStringLiteral("dark")));

    return palette;
}


QColor UBColorPalette::opaque(const QColor& color)
{
    QColor result = color.toRgb();
    result.setAlpha(255);
    return result;
}


QColor UBColorPalette::darkVariant(const QColor& colorOnLightBackground)
{
    QColor color = opaque(colorOnLightBackground);

    ColorComponent h, s, l;
    color.getHslF(&h, &s, &l);

    // near black (or very dark grey): use white on a dark background
    if (l < 0.2 && (s < 0.3 || l < 0.08))
    {
        return QColor(Qt::white);
    }

    // very dark colors: raise the lightness until they are visible on black
    if (luminance(color) < 0.18)
    {
        if (h < 0)
            h = 0; // achromatic

        while (luminance(color) < 0.18 && l < 0.85)
        {
            l = qMin<ColorComponent>(l + 0.02, 0.85);
            color = QColor::fromHslF(h, s, l);
        }
    }

    return opaque(color);
}


QColor UBColorPalette::lightVariant(const QColor& colorOnDarkBackground)
{
    QColor color = opaque(colorOnDarkBackground);

    ColorComponent h, s, l;
    color.getHslF(&h, &s, &l);

    // near white (or very light grey): use black on a light background
    if (l > 0.8 && (s < 0.3 || l > 0.92))
    {
        return QColor(Qt::black);
    }

    // very light colors: lower the lightness until they are visible on white
    if (luminance(color) > 0.8)
    {
        if (h < 0)
            h = 0; // achromatic

        while (luminance(color) > 0.7 && l > 0.25)
        {
            l = qMax<ColorComponent>(l - 0.02, 0.25);
            color = QColor::fromHslF(h, s, l);
        }
    }

    return opaque(color);
}


UBColorPalette UBColorPalette::builtInDefault()
{
    UBSettings* settings = UBSettings::settings();
    const int size = UBSettings::defaultColorPaletteSize;

    UBColorPalette palette(QObject::tr("Default"));
    palette.penLight = opaqueList(settings->defaultPenColors(false)).mid(0, size);
    palette.penDark = opaqueList(settings->defaultPenColors(true)).mid(0, size);
    palette.markerLight = opaqueList(settings->defaultMarkerColors(false)).mid(0, size);
    palette.markerDark = opaqueList(settings->defaultMarkerColors(true)).mid(0, size);

    // safety net in case the settings contain fewer colors
    UBColorPalette black;
    black.penLight = black.penDark = black.markerLight = black.markerDark = QList<QColor>() << QColor(Qt::black);
    palette.normalize(black);

    return palette;
}


UBColorPalette UBColorPalette::builtInRainbow()
{
    const QStringList penColors = {
        "#000000", "#E53935", "#FB8C00", "#FDD835", "#43A047",
        "#00ACC1", "#1E88E5", "#3949AB", "#8E24AA", "#D81B60"
    };

    const QStringList markerColors = {
        "#FFFF00", "#FF1744", "#FF9100", "#FFD600", "#00E676",
        "#00E5FF", "#2979FF", "#3D5AFE", "#D500F9", "#F50057"
    };

    UBColorPalette palette(QObject::tr("Rainbow"));

    const int size = qMin(maxSize(), int(penColors.size()));

    for (int i = 0; i < size; ++i)
    {
        const QColor pen = colorFromString(penColors.at(i));
        const QColor marker = colorFromString(markerColors.at(i));

        // yellow is hard to read on white, so use a darker variant for the pen
        const QColor penOnLight = (i == 3) ? QColor("#C8A600") : pen;

        palette.appendSlot(penOnLight, darkVariant(pen), marker, darkVariant(marker));
    }

    palette.normalize(builtInDefault());
    return palette;
}


// ---------------------------------------------------------------------------
// UBColorPaletteManager
// ---------------------------------------------------------------------------

UBColorPaletteManager* UBColorPaletteManager::sInstance = nullptr;


UBColorPaletteManager* UBColorPaletteManager::manager()
{
    if (!sInstance)
    {
        sInstance = new UBColorPaletteManager(qApp);
    }

    return sInstance;
}


void UBColorPaletteManager::destroy()
{
    delete sInstance;
    sInstance = nullptr;
}


UBColorPaletteManager::UBColorPaletteManager(QObject* parent)
    : QObject(parent)
{
    load();
}


void UBColorPaletteManager::connectSignals()
{
    if (mSignalsConnected)
        return;

    mSignalsConnected = true;

    connect(UBDrawingController::drawingController(), &UBDrawingController::colorPaletteChanged,
            this, &UBColorPaletteManager::captureFromSettings);
}


QStringList UBColorPaletteManager::paletteNames() const
{
    QStringList names;

    for (const UBColorPalette& palette : mPalettes)
    {
        names << palette.name;
    }

    return names;
}


UBColorPalette UBColorPaletteManager::activePalette() const
{
    return mPalettes.value(mActiveIndex);
}


QString UBColorPaletteManager::uniqueName(const QString& base, const QStringList& existing)
{
    QString name = base.trimmed();

    if (name.isEmpty())
    {
        name = tr("Palette");
    }

    if (!existing.contains(name))
    {
        return name;
    }

    for (int i = 2; ; ++i)
    {
        const QString candidate = QStringLiteral("%1 (%2)").arg(name).arg(i);

        if (!existing.contains(candidate))
        {
            return candidate;
        }
    }
}


void UBColorPaletteManager::load()
{
    UBSettings* settings = UBSettings::settings();
    const UBColorPalette filler = UBColorPalette::builtInDefault();

    mPalettes.clear();

    const QByteArray json = settings->boardColorPalettes->get().toString().toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray array = doc.isObject()
            ? doc.object().value(QStringLiteral("palettes")).toArray()
            : doc.array();

    QStringList names;

    for (const QJsonValue& value : array)
    {
        UBColorPalette palette = UBColorPalette::fromJson(value.toObject());
        palette.normalize(filler);
        palette.name = uniqueName(palette.name, names);
        names << palette.name;
        mPalettes << palette;
    }

    bool needsSave = false;

    if (mPalettes.isEmpty())
    {
        // first start: the current colors (possibly customized by the user)
        // become the "Default" palette, and a rainbow palette is offered too.
        UBColorPalette current = paletteFromSettings();
        current.name = tr("Default");
        current.normalize(filler);
        mPalettes << current;

        UBColorPalette rainbow = UBColorPalette::builtInRainbow();
        rainbow.name = uniqueName(rainbow.name, paletteNames());
        mPalettes << rainbow;

        needsSave = true;
    }

    const QString activeName = settings->boardActiveColorPalette->get().toString();
    mActiveIndex = qMax(0, paletteNames().indexOf(activeName));

    if (needsSave || activeName != mPalettes.at(mActiveIndex).name)
    {
        save();
    }

    // the Board/...SelectedColors settings are the reference for the active palette
    captureFromSettings();
}


void UBColorPaletteManager::save()
{
    QJsonArray array;

    for (const UBColorPalette& palette : mPalettes)
    {
        array.append(palette.toJson());
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("palettes"), array);

    UBSettings* settings = UBSettings::settings();
    settings->boardColorPalettes->set(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    settings->boardActiveColorPalette->set(activePalette().name);
}


UBColorPalette UBColorPaletteManager::paletteFromSettings() const
{
    UBSettings* settings = UBSettings::settings();

    UBColorPalette palette;
    palette.penLight = opaqueList(settings->penColors(false));
    palette.penDark = opaqueList(settings->penColors(true));
    palette.markerLight = opaqueList(settings->markerColors(false));
    palette.markerDark = opaqueList(settings->markerColors(true));
    return palette;
}


void UBColorPaletteManager::captureFromSettings()
{
    if (mApplying || mPalettes.isEmpty())
        return;

    UBColorPalette current = paletteFromSettings();
    current.name = mPalettes.at(mActiveIndex).name;
    current.normalize(mPalettes.at(mActiveIndex));

    if (!current.sameColors(mPalettes.at(mActiveIndex)))
    {
        mPalettes[mActiveIndex] = current;
        save();
        emit palettesChanged();
    }
}


void UBColorPaletteManager::applyActiveToSettings()
{
    if (mPalettes.isEmpty())
        return;

    UBSettings* settings = UBSettings::settings();
    const UBColorPalette palette = activePalette();

    mApplying = true;

    settings->setColorPaletteSize(palette.size());

    const int storedSize = UBSettings::maxColorPaletteSize;
    settings->boardPenLightBackgroundSelectedColors->setColors(padded(palette.penLight, settings->defaultPenColors(false), storedSize));
    settings->boardPenDarkBackgroundSelectedColors->setColors(padded(palette.penDark, settings->defaultPenColors(true), storedSize));
    settings->boardMarkerLightBackgroundSelectedColors->setColors(padded(palette.markerLight, settings->defaultMarkerColors(false), storedSize));
    settings->boardMarkerDarkBackgroundSelectedColors->setColors(padded(palette.markerDark, settings->defaultMarkerColors(true), storedSize));

    mApplying = false;

    refreshBoardColors();
}


void UBColorPaletteManager::refreshBoardColors()
{
    UBDrawingController* drawingController = UBDrawingController::drawingController();
    drawingController->refreshColorPalette();

    const int index = drawingController->currentToolColorIndex();

    if (index >= 0)
    {
        drawingController->setColorIndex(index);
    }
}


void UBColorPaletteManager::setPalettes(const QList<UBColorPalette>& palettes, int activeIndex)
{
    if (palettes.isEmpty())
        return;

    const UBColorPalette filler = UBColorPalette::builtInDefault();

    mPalettes.clear();
    QStringList names;

    for (UBColorPalette palette : palettes)
    {
        palette.normalize(filler);
        palette.name = uniqueName(palette.name, names);
        names << palette.name;
        mPalettes << palette;
    }

    mActiveIndex = qBound(0, activeIndex, mPalettes.size() - 1);
    mHasCustomPen = false;
    mHasCustomMarker = false;

    save();
    applyActiveToSettings();

    emit palettesChanged();
    emit activePaletteChanged(mActiveIndex);
}


void UBColorPaletteManager::setActivePalette(int index)
{
    if (index < 0 || index >= mPalettes.size())
        return;

    // make sure pending edits of the current palette are not lost
    captureFromSettings();

    mActiveIndex = index;
    mHasCustomPen = false;
    mHasCustomMarker = false;

    save();
    applyActiveToSettings();

    emit activePaletteChanged(mActiveIndex);

    UBApplication::showMessage(tr("Color palette: %1").arg(activePalette().name));
}


void UBColorPaletteManager::selectNextPalette()
{
    if (mPalettes.isEmpty())
        return;

    setActivePalette((mActiveIndex + 1) % mPalettes.size());
}


void UBColorPaletteManager::selectPreviousPalette()
{
    if (mPalettes.isEmpty())
        return;

    setActivePalette((mActiveIndex + mPalettes.size() - 1) % mPalettes.size());
}


bool UBColorPaletteManager::isMarkerToolActive() const
{
    return UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker;
}


QColor UBColorPaletteManager::currentToolColor() const
{
    const bool marker = isMarkerToolActive();
    const bool dark = UBSettings::settings()->isDarkBackground();

    if (hasCustomColor(marker))
    {
        return UBColorPalette::opaque(customColor(marker, dark));
    }

    if (marker)
    {
        return UBColorPalette::opaque(UBSettings::settings()->markerColor(dark));
    }

    return UBColorPalette::opaque(UBSettings::settings()->penColor(dark));
}


bool UBColorPaletteManager::hasCustomColor(bool marker) const
{
    return marker ? mHasCustomMarker : mHasCustomPen;
}


QColor UBColorPaletteManager::customColor(bool marker, bool onDarkBackground) const
{
    const int variant = onDarkBackground ? 1 : 0;

    if (marker)
    {
        QColor color = mCustomMarker[variant];
        color.setAlphaF(UBSettings::settings()->boardMarkerAlpha->get().toDouble());
        return color;
    }

    return mCustomPen[variant];
}


void UBColorPaletteManager::clearCustomColor()
{
    if (isMarkerToolActive())
    {
        mHasCustomMarker = false;
    }
    else
    {
        mHasCustomPen = false;
    }
}


void UBColorPaletteManager::useColor(const QColor& color)
{
    if (!color.isValid())
        return;

    UBDrawingController* drawingController = UBDrawingController::drawingController();
    const bool marker = isMarkerToolActive();

    if (!marker && drawingController->stylusTool() != UBStylusTool::Pen
            && drawingController->stylusTool() != UBStylusTool::Line
            && drawingController->stylusTool() != UBStylusTool::Shape)
    {
        drawingController->setStylusTool(UBStylusTool::Pen);
    }

    const bool dark = UBSettings::settings()->isDarkBackground();
    const QColor picked = UBColorPalette::opaque(color);
    const QColor light = dark ? UBColorPalette::lightVariant(picked) : picked;
    const QColor darkColor = dark ? picked : UBColorPalette::darkVariant(picked);

    if (marker)
    {
        mHasCustomMarker = true;
        mCustomMarker[0] = light;
        mCustomMarker[1] = darkColor;
    }
    else
    {
        mHasCustomPen = true;
        mCustomPen[0] = light;
        mCustomPen[1] = darkColor;
    }

    // the board controller applies the custom colors when the palette is refreshed
    drawingController->refreshColorPalette();

    if (UBApplication::boardController)
    {
        if (marker)
        {
            UBApplication::boardController->setMarkerColorOnLightBackground(customColor(true, false));
            UBApplication::boardController->setMarkerColorOnDarkBackground(customColor(true, true));
        }
        else
        {
            UBApplication::boardController->setPenColorOnLightBackground(customColor(false, false));
            UBApplication::boardController->setPenColorOnDarkBackground(customColor(false, true));
        }
    }
}


void UBColorPaletteManager::addColorToActivePalette(const QColor& color)
{
    if (!color.isValid() || mPalettes.isEmpty())
        return;

    captureFromSettings();

    const bool marker = isMarkerToolActive();
    const bool dark = UBSettings::settings()->isDarkBackground();
    const QColor picked = UBColorPalette::opaque(color);
    const QColor light = dark ? UBColorPalette::lightVariant(picked) : picked;
    const QColor darkColor = dark ? picked : UBColorPalette::darkVariant(picked);

    UBColorPalette& palette = mPalettes[mActiveIndex];
    int slot = 0;

    if (palette.size() < UBColorPalette::maxSize())
    {
        palette.appendColor(light, darkColor);
        slot = palette.size() - 1;
        UBApplication::showMessage(tr("Color added to palette \"%1\".").arg(palette.name));
    }
    else
    {
        slot = qBound(0, UBDrawingController::drawingController()->currentToolColorIndex(), palette.size() - 1);
        palette.setSlot(slot, marker, light, darkColor);
        UBApplication::showMessage(tr("Palette \"%1\" is full (%2 colors): color %3 was replaced.")
                                   .arg(palette.name)
                                   .arg(palette.size())
                                   .arg(slot + 1));
    }

    save();
    applyActiveToSettings();
    selectSlot(slot);

    emit palettesChanged();
}


void UBColorPaletteManager::setActivePaletteSlot(int index, const QColor& color)
{
    if (!color.isValid() || mPalettes.isEmpty())
        return;

    captureFromSettings();

    UBColorPalette& palette = mPalettes[mActiveIndex];

    if (index < 0 || index >= palette.size())
        return;

    const bool marker = isMarkerToolActive();
    const bool dark = UBSettings::settings()->isDarkBackground();
    const QColor picked = UBColorPalette::opaque(color);

    if (marker)
    {
        if (dark)
            palette.markerDark[index] = picked;
        else
            palette.markerLight[index] = picked;
    }
    else
    {
        if (dark)
            palette.penDark[index] = picked;
        else
            palette.penLight[index] = picked;
    }

    save();
    applyActiveToSettings();
    selectSlot(index);

    emit palettesChanged();
}


void UBColorPaletteManager::selectSlot(int index)
{
    if (isMarkerToolActive())
    {
        mHasCustomMarker = false;
    }
    else
    {
        mHasCustomPen = false;
    }

    if (UBApplication::boardController)
    {
        UBApplication::boardController->setColorIndex(index);
    }
}
