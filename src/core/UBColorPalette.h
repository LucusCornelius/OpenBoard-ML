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

#ifndef UBCOLORPALETTE_H
#define UBCOLORPALETTE_H

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * A named color palette as shown in the board toolbar.
 *
 * Every palette slot has four colors: pen and marker, each with a variant for
 * light and for dark page backgrounds. All four lists always have the same
 * length. Marker colors are stored opaque; the marker opacity is a global
 * setting applied by UBColorListSetting.
 */
class UBColorPalette
{
    public:
        UBColorPalette() = default;
        explicit UBColorPalette(const QString& name);

        QString name;
        QList<QColor> penLight;
        QList<QColor> penDark;
        QList<QColor> markerLight;
        QList<QColor> markerDark;

        int size() const;

        // Make all lists the same length (clamped to the allowed palette size range),
        // padding missing entries from the filler palette.
        void normalize(const UBColorPalette& filler);
        void resize(int size, const UBColorPalette& filler);

        // Append a slot with the same color for pen and marker.
        void appendColor(const QColor& light, const QColor& dark);
        void appendSlot(const QColor& penLightColor, const QColor& penDarkColor,
                        const QColor& markerLightColor, const QColor& markerDarkColor);
        void setSlot(int index, bool marker, const QColor& light, const QColor& dark);
        void removeSlot(int index);
        void moveSlot(int from, int to);

        bool sameColors(const UBColorPalette& other) const;

        QJsonObject toJson() const;
        static UBColorPalette fromJson(const QJsonObject& json);

        // Color helpers used to derive the counterpart for the other background.
        static QColor opaque(const QColor& color);
        static QColor darkVariant(const QColor& colorOnLightBackground);
        static QColor lightVariant(const QColor& colorOnDarkBackground);

        // Built-in palettes
        static UBColorPalette builtInDefault();
        static UBColorPalette builtInRainbow();

        static int minSize();
        static int maxSize();
};


/**
 * Keeps the list of named palettes, persists them in the settings
 * (Board/ColorPalettes, Board/ActiveColorPalette) and keeps the active palette
 * in sync with the Board/...SelectedColors settings used by the rest of OpenBoard.
 */
class UBColorPaletteManager : public QObject
{
    Q_OBJECT

    public:
        static UBColorPaletteManager* manager();
        static void destroy();

        // Connect to the drawing controller so that changes made elsewhere
        // (e.g. the color preferences dialog) are captured in the active palette.
        void connectSignals();

        QList<UBColorPalette> palettes() const { return mPalettes; }
        QStringList paletteNames() const;
        int activeIndex() const { return mActiveIndex; }
        UBColorPalette activePalette() const;

        void setPalettes(const QList<UBColorPalette>& palettes, int activeIndex);
        void setActivePalette(int index);

        static QString uniqueName(const QString& base, const QStringList& existing);

        // Board operations
        bool isMarkerToolActive() const;
        QColor currentToolColor() const;
        void useColor(const QColor& color);
        void addColorToActivePalette(const QColor& color);
        void setActivePaletteSlot(int index, const QColor& color);

        // Transient ("custom") tool colors set with useColor(); not part of any palette.
        bool hasCustomColor(bool marker) const;
        QColor customColor(bool marker, bool onDarkBackground) const;

    public slots:
        void selectNextPalette();
        void selectPreviousPalette();
        void clearCustomColor();
        void captureFromSettings();

    signals:
        void palettesChanged();
        void activePaletteChanged(int index);

    private:
        UBColorPaletteManager(QObject* parent = nullptr);

        void load();
        void save();
        void applyActiveToSettings();
        UBColorPalette paletteFromSettings() const;
        void refreshBoardColors();
        void selectSlot(int index);

        static UBColorPaletteManager* sInstance;

        QList<UBColorPalette> mPalettes;
        int mActiveIndex{0};
        bool mApplying{false};
        bool mSignalsConnected{false};

        bool mHasCustomPen{false};
        bool mHasCustomMarker{false};
        QColor mCustomPen[2];
        QColor mCustomMarker[2];
};

#endif // UBCOLORPALETTE_H
