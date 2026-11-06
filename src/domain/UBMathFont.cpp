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

#include "UBMathFont.h"

#include <QDebug>
#include <QFile>
#include <QTransform>

#include "core/memcheck.h"

namespace
{
    /** Big endian reader for OpenType tables, safe against truncated data. */
    class TableReader
    {
        public:
            explicit TableReader(const QByteArray& data) : mData(data) {}

            bool has(int offset, int size) const
            {
                return offset >= 0 && size >= 0 && offset + size <= mData.size();
            }

            quint16 u16(int offset) const
            {
                if (!has(offset, 2))
                {
                    return 0;
                }

                const uchar* p = reinterpret_cast<const uchar*>(mData.constData()) + offset;
                return quint16((p[0] << 8) | p[1]);
            }

            qint16 s16(int offset) const
            {
                return qint16(u16(offset));
            }

            quint32 u32(int offset) const
            {
                return (quint32(u16(offset)) << 16) | u16(offset + 2);
            }

            QByteArray tag(int offset) const
            {
                return has(offset, 4) ? mData.mid(offset, 4) : QByteArray();
            }

            /** The glyphs of a Coverage table, in coverage order. */
            QList<quint32> coverage(int offset) const
            {
                QList<quint32> glyphs;
                const quint16 format = u16(offset);

                if (format == 1)
                {
                    const int count = u16(offset + 2);

                    for (int i = 0; i < count; ++i)
                    {
                        glyphs << u16(offset + 4 + 2 * i);
                    }
                }
                else if (format == 2)
                {
                    const int count = u16(offset + 2);

                    for (int i = 0; i < count; ++i)
                    {
                        const int record = offset + 4 + 6 * i;
                        const quint16 start = u16(record);
                        const quint16 end = u16(record + 2);

                        for (quint32 g = start; g <= end && end >= start; ++g)
                        {
                            glyphs << g;
                        }
                    }
                }

                return glyphs;
            }

        private:
            const QByteArray& mData;
    };
}

const UBMathFont& UBMathFont::font()
{
    static const UBMathFont instance;
    return instance;
}

UBMathFont::UBMathFont()
{
    QFile file(":/math/latinmodern-math.otf");

    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "UBMathFont: cannot open" << file.fileName();
        return;
    }

    const QByteArray data = file.readAll();

    // first load at any size to learn the design grid, then use one pixel per design unit
    QRawFont probe(data, 100, QFont::PreferNoHinting);

    if (!probe.isValid())
    {
        qWarning() << "UBMathFont: invalid font data";
        return;
    }

    mUnitsPerEm = probe.unitsPerEm() > 0 ? probe.unitsPerEm() : 1000;
    mRawFont = QRawFont(data, mUnitsPerEm, QFont::PreferNoHinting);
    mValid = mRawFont.isValid();

    if (mValid)
    {
        readMathTable(mRawFont.fontTable("MATH"));
        readScriptAlternates(mRawFont.fontTable("GSUB"));
    }
}

void UBMathFont::readMathTable(const QByteArray& math)
{
    TableReader r(math);

    if (!r.has(0, 10) || r.u16(0) != 1)
    {
        qWarning() << "UBMathFont: the font has no MATH table, using default constants";
        return;
    }

    const qreal em = mUnitsPerEm;

    // ---------------------------------------------------------------- MathConstants
    const int c = r.u16(4);
    // MathValueRecord number i (from mathLeading on) starts at c + 8 + 4 i
    auto value = [&](int i) { return r.s16(c + 8 + 4 * i) / em; };

    Constants& k = mConstants;
    k.scriptScale = r.s16(c) / 100.0;
    k.scriptScriptScale = r.s16(c + 2) / 100.0;
    k.displayOperatorMinHeight = r.u16(c + 6) / em;
    k.axisHeight = value(1);
    k.accentBaseHeight = value(2);
    k.subscriptShiftDown = value(4);
    k.subscriptTopMax = value(5);
    k.subscriptBaselineDropMin = value(6);
    k.superscriptShiftUp = value(7);
    k.superscriptShiftUpCramped = value(8);
    k.superscriptBottomMin = value(9);
    k.superscriptBaselineDropMax = value(10);
    k.subSuperscriptGapMin = value(11);
    k.superscriptBottomMaxWithSubscript = value(12);
    k.spaceAfterScript = value(13);
    k.upperLimitGapMin = value(14);
    k.upperLimitBaselineRiseMin = value(15);
    k.lowerLimitGapMin = value(16);
    k.lowerLimitBaselineDropMin = value(17);
    k.fractionNumeratorShiftUp = value(28);
    k.fractionNumeratorDisplayStyleShiftUp = value(29);
    k.fractionDenominatorShiftDown = value(30);
    k.fractionDenominatorDisplayStyleShiftDown = value(31);
    k.fractionNumeratorGapMin = value(32);
    k.fractionNumDisplayStyleGapMin = value(33);
    k.fractionRuleThickness = value(34);
    k.fractionDenominatorGapMin = value(35);
    k.fractionDenomDisplayStyleGapMin = value(36);
    k.radicalVerticalGap = value(45);
    k.radicalDisplayStyleVerticalGap = value(46);
    k.radicalRuleThickness = value(47);
    k.radicalExtraAscender = value(48);

    if (k.scriptScale <= 0)
    {
        k.scriptScale = 0.7;
    }

    if (k.scriptScriptScale <= 0)
    {
        k.scriptScriptScale = 0.5;
    }

    // ---------------------------------------------------------------- MathGlyphInfo
    const int info = r.u16(6);

    if (info)
    {
        auto readValues = [&](int table, QHash<quint32, qreal>& target) {
            if (!table)
            {
                return;
            }

            const QList<quint32> glyphs = r.coverage(table + r.u16(table));
            const int count = qMin<int>(r.u16(table + 2), glyphs.size());

            for (int i = 0; i < count; ++i)
            {
                target.insert(glyphs.at(i), r.s16(table + 4 + 4 * i) / em);
            }
        };

        const int italics = r.u16(info);
        const int accents = r.u16(info + 2);
        readValues(italics ? info + italics : 0, mItalicCorrections);
        readValues(accents ? info + accents : 0, mTopAccents);
    }

    // ---------------------------------------------------------------- MathVariants
    const int variants = r.u16(8);

    if (variants)
    {
        mMinConnectorOverlap = r.u16(variants) / em;
        const int verticalCoverage = r.u16(variants + 2);
        const int horizontalCoverage = r.u16(variants + 4);
        const int verticalCount = r.u16(variants + 6);
        const int horizontalCount = r.u16(variants + 8);

        auto readConstructions = [&](int coverage, int count, int firstOffset,
                                     QHash<quint32, QList<Variant>>& variantTarget,
                                     QHash<quint32, QList<Part>>& assemblyTarget) {
            if (!coverage)
            {
                return;
            }

            const QList<quint32> glyphs = r.coverage(variants + coverage);

            for (int i = 0; i < qMin<int>(count, glyphs.size()); ++i)
            {
                const int construction = variants + r.u16(firstOffset + 2 * i);
                const int assemblyOffset = r.u16(construction);
                const int variantCount = r.u16(construction + 2);
                QList<Variant> list;

                for (int v = 0; v < variantCount; ++v)
                {
                    const int record = construction + 4 + 4 * v;
                    list << Variant{r.u16(record), r.u16(record + 2) / em};
                }

                if (!list.isEmpty())
                {
                    variantTarget.insert(glyphs.at(i), list);
                }

                if (assemblyOffset)
                {
                    const int assembly = construction + assemblyOffset;
                    const int partCount = r.u16(assembly + 4);
                    QList<Part> parts;

                    for (int p = 0; p < partCount; ++p)
                    {
                        const int record = assembly + 6 + 10 * p;
                        parts << Part{r.u16(record), r.u16(record + 2) / em, r.u16(record + 4) / em,
                                      r.u16(record + 6) / em, (r.u16(record + 8) & 1) != 0};
                    }

                    if (!parts.isEmpty())
                    {
                        assemblyTarget.insert(glyphs.at(i), parts);
                    }
                }
            }
        };

        const int verticalOffsets = variants + 10;
        const int horizontalOffsets = verticalOffsets + 2 * verticalCount;
        readConstructions(verticalCoverage, verticalCount, verticalOffsets, mVerticalVariants, mVerticalAssemblies);
        readConstructions(horizontalCoverage, horizontalCount, horizontalOffsets, mHorizontalVariants, mHorizontalAssemblies);
    }
}

void UBMathFont::readScriptAlternates(const QByteArray& gsub)
{
    TableReader r(gsub);

    if (!r.has(0, 10))
    {
        return;
    }

    const int featureList = r.u16(6);
    const int lookupList = r.u16(8);
    QList<int> lookups;

    for (int i = 0; i < r.u16(featureList); ++i)
    {
        const int record = featureList + 2 + 6 * i;

        if (r.tag(record) == "ssty")
        {
            const int feature = featureList + r.u16(record + 4);

            for (int l = 0; l < r.u16(feature + 2); ++l)
            {
                lookups << r.u16(feature + 4 + 2 * l);
            }
        }
    }

    for (int index : lookups)
    {
        const int lookup = lookupList + r.u16(lookupList + 2 + 2 * index);
        const int lookupType = r.u16(lookup);

        for (int s = 0; s < r.u16(lookup + 4); ++s)
        {
            int subtable = lookup + r.u16(lookup + 6 + 2 * s);
            int type = lookupType;

            if (type == 7)
            {
                // extension: the real lookup type and a 32 bit offset
                type = r.u16(subtable + 2);
                subtable += int(r.u32(subtable + 4));
            }

            const QList<quint32> glyphs = r.coverage(subtable + r.u16(subtable + 2));
            const int format = r.u16(subtable);

            if (type == 3 && format == 1)
            {
                // alternate substitution: one alternate per script level
                for (int g = 0; g < qMin<int>(r.u16(subtable + 4), glyphs.size()); ++g)
                {
                    const int set = subtable + r.u16(subtable + 6 + 2 * g);
                    QList<quint32> alternates;

                    for (int a = 0; a < r.u16(set); ++a)
                    {
                        alternates << r.u16(set + 2 + 2 * a);
                    }

                    mScriptAlternates.insert(glyphs.at(g), alternates);
                }
            }
            else if (type == 1 && format == 1)
            {
                const quint16 delta = r.u16(subtable + 4);

                for (quint32 g : glyphs)
                {
                    mScriptAlternates.insert(g, {quint16(g + delta)});
                }
            }
            else if (type == 1 && format == 2)
            {
                for (int g = 0; g < qMin<int>(r.u16(subtable + 4), glyphs.size()); ++g)
                {
                    mScriptAlternates.insert(glyphs.at(g), {r.u16(subtable + 6 + 2 * g)});
                }
            }
        }
    }
}

quint32 UBMathFont::glyph(char32_t codePoint) const
{
    if (!mValid)
    {
        return 0;
    }

    const auto it = mGlyphs.constFind(codePoint);

    if (it != mGlyphs.constEnd())
    {
        return *it;
    }

    const QList<quint32> indexes = mRawFont.glyphIndexesForString(QString::fromUcs4(&codePoint, 1));
    const quint32 index = indexes.isEmpty() ? 0 : indexes.first();
    mGlyphs.insert(codePoint, index);
    return index;
}

quint32 UBMathFont::scriptGlyph(quint32 glyph, int level) const
{
    if (level <= 0)
    {
        return glyph;
    }

    const auto it = mScriptAlternates.constFind(glyph);

    if (it == mScriptAlternates.constEnd() || it->isEmpty())
    {
        return glyph;
    }

    return it->at(qMin<int>(level, it->size()) - 1);
}

qreal UBMathFont::advance(quint32 glyph) const
{
    const auto it = mAdvances.constFind(glyph);

    if (it != mAdvances.constEnd())
    {
        return *it;
    }

    const QList<QPointF> advances = mRawFont.advancesForGlyphIndexes(QList<quint32>{glyph}, QRawFont::UseDesignMetrics);
    const qreal value = advances.isEmpty() ? 0 : advances.first().x() / mUnitsPerEm;
    mAdvances.insert(glyph, value);
    return value;
}

QPainterPath UBMathFont::path(quint32 glyph) const
{
    const auto it = mPaths.constFind(glyph);

    if (it != mPaths.constEnd())
    {
        return *it;
    }

    QPainterPath outline = QTransform::fromScale(1.0 / mUnitsPerEm, 1.0 / mUnitsPerEm).map(mRawFont.pathForGlyph(glyph));
    outline.setFillRule(Qt::WindingFill);
    mPaths.insert(glyph, outline);
    return outline;
}

QRectF UBMathFont::inkBounds(quint32 glyph) const
{
    return path(glyph).boundingRect();
}

qreal UBMathFont::italicCorrection(quint32 glyph) const
{
    return mItalicCorrections.value(glyph, 0);
}

qreal UBMathFont::topAccentAttachment(quint32 glyph) const
{
    return mTopAccents.value(glyph, -1);
}

QList<UBMathFont::Variant> UBMathFont::variants(quint32 glyph, bool vertical) const
{
    const QList<Variant> list = (vertical ? mVerticalVariants : mHorizontalVariants).value(glyph);

    if (!list.isEmpty())
    {
        return list;
    }

    const QRectF bounds = inkBounds(glyph);
    return {Variant{glyph, vertical ? bounds.height() : advance(glyph)}};
}

QList<UBMathFont::Part> UBMathFont::assembly(quint32 glyph, bool vertical) const
{
    return (vertical ? mVerticalAssemblies : mHorizontalAssemblies).value(glyph);
}
