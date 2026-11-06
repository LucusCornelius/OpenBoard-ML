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

#ifndef UBMATHFONT_H
#define UBMATHFONT_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QRawFont>
#include <QRectF>

/**
 * The bundled OpenType maths font (Latin Modern Math, the Computer Modern look of TeX)
 * with the data of its MATH table: the typesetting constants, italic corrections,
 * accent attachment points, the larger glyph variants and glyph assemblies, and the
 * script style alternates ('ssty' feature of the GSUB table).
 *
 * All lengths are in em (1.0 = the font size), y axis down, glyph origin at the left end
 * of the baseline, as in QPainterPath.
 */
class UBMathFont
{
    public:
        /** The MathConstants of the font, in em (percentages as factors). */
        struct Constants
        {
            qreal scriptScale{0.7};
            qreal scriptScriptScale{0.5};
            qreal displayOperatorMinHeight{1.3};
            qreal axisHeight{0.25};
            qreal accentBaseHeight{0.45};
            qreal subscriptShiftDown{0.247};
            qreal subscriptTopMax{0.344};
            qreal subscriptBaselineDropMin{0.2};
            qreal superscriptShiftUp{0.363};
            qreal superscriptShiftUpCramped{0.289};
            qreal superscriptBottomMin{0.108};
            qreal superscriptBaselineDropMax{0.25};
            qreal subSuperscriptGapMin{0.16};
            qreal superscriptBottomMaxWithSubscript{0.344};
            qreal spaceAfterScript{0.056};
            qreal upperLimitGapMin{0.2};
            qreal upperLimitBaselineRiseMin{0.111};
            qreal lowerLimitGapMin{0.167};
            qreal lowerLimitBaselineDropMin{0.6};
            qreal fractionNumeratorShiftUp{0.394};
            qreal fractionNumeratorDisplayStyleShiftUp{0.677};
            qreal fractionDenominatorShiftDown{0.345};
            qreal fractionDenominatorDisplayStyleShiftDown{0.686};
            qreal fractionNumeratorGapMin{0.04};
            qreal fractionNumDisplayStyleGapMin{0.12};
            qreal fractionRuleThickness{0.04};
            qreal fractionDenominatorGapMin{0.04};
            qreal fractionDenomDisplayStyleGapMin{0.12};
            qreal radicalVerticalGap{0.05};
            qreal radicalDisplayStyleVerticalGap{0.148};
            qreal radicalRuleThickness{0.04};
            qreal radicalExtraAscender{0.04};
        };

        struct Variant
        {
            quint32 glyph;
            qreal size;
        };

        struct Part
        {
            quint32 glyph;
            qreal startConnector;
            qreal endConnector;
            qreal fullAdvance;
            bool extender;
        };

        static const UBMathFont& font();

        bool isValid() const { return mValid; }
        const Constants& constants() const { return mConstants; }

        /** The glyph of a Unicode code point, 0 if the font does not have it. */
        quint32 glyph(char32_t codePoint) const;

        /** The script style alternate (level 1 = script, 2 = scriptscript) or the glyph itself. */
        quint32 scriptGlyph(quint32 glyph, int level) const;

        qreal advance(quint32 glyph) const;
        QRectF inkBounds(quint32 glyph) const;
        QPainterPath path(quint32 glyph) const;
        qreal italicCorrection(quint32 glyph) const;

        /** Horizontal position for a top accent, or a negative value if the font has none. */
        qreal topAccentAttachment(quint32 glyph) const;

        /** Larger variants (the glyph itself first), measured in the direction of growth. */
        QList<Variant> variants(quint32 glyph, bool vertical) const;

        /** Parts to build an arbitrarily large glyph (bottom to top / left to right), may be empty. */
        QList<Part> assembly(quint32 glyph, bool vertical) const;
        qreal minConnectorOverlap() const { return mMinConnectorOverlap; }

    private:
        UBMathFont();

        void readMathTable(const QByteArray& math);
        void readScriptAlternates(const QByteArray& gsub);

        bool mValid{false};
        qreal mUnitsPerEm{1000};
        QRawFont mRawFont;
        Constants mConstants;
        qreal mMinConnectorOverlap{0};

        QHash<quint32, qreal> mItalicCorrections;
        QHash<quint32, qreal> mTopAccents;
        QHash<quint32, QList<Variant>> mVerticalVariants;
        QHash<quint32, QList<Variant>> mHorizontalVariants;
        QHash<quint32, QList<Part>> mVerticalAssemblies;
        QHash<quint32, QList<Part>> mHorizontalAssemblies;
        QHash<quint32, QList<quint32>> mScriptAlternates;

        mutable QHash<char32_t, quint32> mGlyphs;
        mutable QHash<quint32, QPainterPath> mPaths;
        mutable QHash<quint32, qreal> mAdvances;
};

#endif // UBMATHFONT_H
