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

#include "UBMathInkLayout.h"

#include <algorithm>
#include <vector>

#include <QCoreApplication>
#include <QHash>
#include <QTransform>

#include "domain/UBMathFont.h"

#include "core/memcheck.h"

namespace
{
    const UBMathFont& mathFont()
    {
        return UBMathFont::font();
    }

    const UBMathFont::Constants& constants()
    {
        return UBMathFont::font().constants();
    }

    // -------------------------------------------------------------------- atoms and styles

    /** The TeX atom classes, they decide the spacing (The TeXbook, chapter 17 and 18). */
    enum class Atom
    {
        Ord, Op, Bin, Rel, Open, Close, Punct, Inner
    };

    /**
     * Space between two atoms (The TeXbook, p. 170): 0 none, 1 thin (3 mu), 2 medium (4 mu),
     * 3 thick (5 mu); negative values only in display and text style, not in scripts.
     * 1 mu = 1/18 em of the current style.
     */
    const int sSpacing[8][8] = {
        //  Ord  Op  Bin  Rel Open Close Punct Inner
        {   0,   1,  -2,  -3,  0,   0,    0,   -1 },  // Ord
        {   1,   1,   0,  -3,  0,   0,    0,   -1 },  // Op
        {  -2,  -2,   0,   0, -2,   0,    0,   -2 },  // Bin
        {  -3,  -3,   0,   0, -3,   0,    0,   -3 },  // Rel
        {   0,   0,   0,   0,  0,   0,    0,    0 },  // Open
        {   0,   1,  -2,  -3,  0,   0,    0,   -1 },  // Close
        {  -1,  -1,   0,  -1, -1,  -1,   -1,   -1 },  // Punct
        {  -1,   1,  -2,  -3, -1,   0,   -1,   -1 }   // Inner
    };

    /** \nulldelimiterspace of plain TeX: 1.2 pt at 10 pt, on both sides of a fraction. */
    const qreal sNullDelimiterSpace = 0.12;

    /** \delimiterfactor 901 and \delimitershortfall 5 pt of plain TeX (rule 19). */
    const qreal sDelimiterFactor = 0.901;
    const qreal sDelimiterShortfall = 0.5;

    /** TeX styles: display, text, script, scriptscript, each normal or cramped. */
    struct Style
    {
        int level{0};
        bool cramped{false};

        qreal size() const
        {
            return level <= 1 ? 1.0 : (level == 2 ? constants().scriptScale : constants().scriptScriptScale);
        }

        int scriptLevel() const { return qMax(0, level - 1); }
        bool isDisplay() const { return level == 0; }
        bool isScript() const { return level >= 2; }
        Style superscript() const { return Style{level <= 1 ? 2 : 3, cramped}; }
        Style subscript() const { return Style{level <= 1 ? 2 : 3, true}; }
        Style numerator() const { return Style{level == 0 ? 1 : (level == 1 ? 2 : 3), cramped}; }
        Style denominator() const { return Style{numerator().level, true}; }
        Style crampedStyle() const { return Style{level, true}; }
    };

    qreal spaceBetween(Atom left, Atom right, const Style& style)
    {
        const int code = sSpacing[int(left)][int(right)];

        if (code == 0 || (code < 0 && style.isScript()))
        {
            return 0;
        }

        return (2 + qAbs(code)) / 18.0 * style.size();
    }

    // -------------------------------------------------------------------- boxes

    /** A laid out piece of a formula. Units em, y axis down, origin at the left end of the baseline. */
    struct Box
    {
        qreal width{0};
        qreal ascent{0};
        qreal descent{0};
        qreal italic{0};
        qreal size{1};
        quint32 glyph{0};
        bool character{false};
        bool largeOperator{false};
        QList<QPainterPath> shapes;
        QList<QRectF> placeholders;

        /** Place another box with its baseline origin at (x, y). The result is a composite box. */
        void place(const Box& other, qreal x, qreal y)
        {
            const QTransform transform = QTransform::fromTranslate(x, y);

            for (const QPainterPath& shape : other.shapes)
            {
                shapes << transform.map(shape);
            }

            for (const QRectF& rect : other.placeholders)
            {
                placeholders << rect.translated(x, y);
            }

            width = qMax(width, x + other.width);
            ascent = qMax(ascent, other.ascent - y);
            descent = qMax(descent, other.descent + y);
            character = false;
            largeOperator = false;
        }

        void append(const Box& other, qreal gap = 0)
        {
            place(other, width + gap, 0);
        }

        /** A rule (fraction bar, vinculum). */
        void addRule(const QRectF& rect)
        {
            QPainterPath path;
            path.setFillRule(Qt::WindingFill);
            path.addRect(rect);
            shapes << path;
            ascent = qMax(ascent, -rect.top());
            descent = qMax(descent, rect.bottom());
            width = qMax(width, rect.right());
        }
    };

    /** A single glyph at the size of the style (script style alternates if the font has them). */
    Box glyphBox(quint32 glyph, const Style& style, bool scriptAlternate = true)
    {
        const qreal s = style.size();
        const quint32 g = scriptAlternate ? mathFont().scriptGlyph(glyph, style.scriptLevel()) : glyph;
        const QPainterPath outline = mathFont().path(g);

        Box box;
        box.width = mathFont().advance(g) * s;
        box.italic = mathFont().italicCorrection(g) * s;
        box.size = s;
        box.glyph = g;
        box.character = true;

        if (!outline.isEmpty())
        {
            const QRectF ink = outline.boundingRect();
            box.shapes << QTransform::fromScale(s, s).map(outline);
            box.ascent = -ink.top() * s;
            box.descent = ink.bottom() * s;
        }

        return box;
    }

    /** Centre a box vertically on the maths axis (delimiters, big operators). */
    Box centredOnAxis(const Box& box, const Style& style)
    {
        const qreal axis = constants().axisHeight * style.size();
        Box result;
        result.place(box, 0, -axis - (box.descent - box.ascent) / 2);
        result.italic = box.italic;
        result.largeOperator = box.largeOperator;
        return result;
    }

    /** A glyph built from the parts of a glyph assembly (OpenType MATH), at least `target` high or wide. */
    Box assemblyBox(const QList<UBMathFont::Part>& parts, qreal target, const Style& style, bool vertical)
    {
        const qreal s = style.size();
        const qreal minOverlap = mathFont().minConnectorOverlap();
        QList<UBMathFont::Part> list;

        for (int repeats = 0; repeats < 64; ++repeats)
        {
            list.clear();

            for (const UBMathFont::Part& part : parts)
            {
                for (int i = 0; i < (part.extender ? repeats : 1); ++i)
                {
                    list << part;
                }
            }

            qreal full = 0;

            for (const UBMathFont::Part& part : list)
            {
                full += part.fullAdvance;
            }

            if (list.size() > 0 && (full - (list.size() - 1) * minOverlap) * s >= target)
            {
                break;
            }
        }

        if (list.isEmpty())
        {
            return Box();
        }

        // the largest possible overlap of each connection, then shrink them evenly to reach the target
        QList<qreal> maxOverlaps;
        qreal full = list.first().fullAdvance;
        qreal shrinkable = 0;

        for (int i = 1; i < list.size(); ++i)
        {
            const qreal overlap = qMax(minOverlap, qMin(list.at(i - 1).endConnector, list.at(i).startConnector));
            maxOverlaps << overlap;
            full += list.at(i).fullAdvance - overlap;
            shrinkable += overlap - minOverlap;
        }

        const qreal needed = target / s - full;
        const qreal t = shrinkable > 0 ? qBound(0.0, needed / shrinkable, 1.0) : 0;

        Box box;
        qreal offset = 0;

        for (int i = 0; i < list.size(); ++i)
        {
            if (i > 0)
            {
                const qreal overlap = maxOverlaps.at(i - 1) - t * (maxOverlaps.at(i - 1) - minOverlap);
                offset += list.at(i - 1).fullAdvance - overlap;
            }

            const Box part = glyphBox(list.at(i).glyph, style, false);

            if (vertical)
            {
                // stacked bottom to top, each glyph with its ink bottom at the offset
                const QRectF ink = mathFont().inkBounds(list.at(i).glyph);
                box.place(part, 0, -offset * s - ink.bottom() * s);
            }
            else
            {
                // left to right, each glyph with its origin at the offset
                box.place(part, offset * s, 0);
            }
        }

        // one shape: the overlapping parts are filled as one, without seams
        QPainterPath joined;
        joined.setFillRule(Qt::WindingFill);

        for (const QPainterPath& shape : box.shapes)
        {
            joined.addPath(shape);
        }

        box.shapes = {joined};
        return box;
    }

    /** The smallest variant of a glyph that is at least `target` high (or wide), else an assembly. */
    Box stretchedGlyph(quint32 glyph, qreal target, const Style& style, bool vertical = true)
    {
        const qreal s = style.size();
        const QList<UBMathFont::Variant> variants = mathFont().variants(glyph, vertical);

        for (const UBMathFont::Variant& variant : variants)
        {
            if (variant.size * s >= target)
            {
                return glyphBox(variant.glyph, style, variant.glyph == glyph);
            }
        }

        const QList<UBMathFont::Part> parts = mathFont().assembly(glyph, vertical);

        if (!parts.isEmpty())
        {
            return assemblyBox(parts, target, style, vertical);
        }

        return glyphBox(variants.isEmpty() ? glyph : variants.last().glyph, style, false);
    }

    /** A delimiter for content of the given extent, centred on the axis (TeX rule 19). */
    Box delimiterBox(char32_t c, qreal ascent, qreal descent, const Style& style)
    {
        const quint32 glyph = mathFont().glyph(c);

        if (!glyph)
        {
            return Box();
        }

        const qreal axis = constants().axisHeight * style.size();
        const qreal delta = qMax(ascent - axis, descent + axis);
        const qreal target = qMax(2 * delta * sDelimiterFactor, 2 * delta - sDelimiterShortfall * style.size());
        return centredOnAxis(stretchedGlyph(glyph, target, style), style);
    }

    Box placeholderBox(const Style& style)
    {
        const qreal s = style.size();
        Box box;
        box.width = 0.6 * s;
        box.ascent = 0.68 * s;
        box.descent = 0.02 * s;
        box.placeholders << QRectF(0.05 * s, -0.68 * s, 0.5 * s, 0.7 * s);
        return box;
    }

    /** A fraction (TeX rule 15, OpenType fraction constants). */
    Box fractionBox(const Box& numerator, const Box& denominator, const Style& style)
    {
        const UBMathFont::Constants& k = constants();
        const qreal s = style.size();
        const qreal axis = k.axisHeight * s;
        const qreal rule = k.fractionRuleThickness * s;
        qreal shiftUp = (style.isDisplay() ? k.fractionNumeratorDisplayStyleShiftUp : k.fractionNumeratorShiftUp) * s;
        qreal shiftDown = (style.isDisplay() ? k.fractionDenominatorDisplayStyleShiftDown : k.fractionDenominatorShiftDown) * s;
        const qreal numeratorGap = (style.isDisplay() ? k.fractionNumDisplayStyleGapMin : k.fractionNumeratorGapMin) * s;
        const qreal denominatorGap = (style.isDisplay() ? k.fractionDenomDisplayStyleGapMin : k.fractionDenominatorGapMin) * s;

        shiftUp = qMax(shiftUp, axis + rule / 2 + numeratorGap + numerator.descent);
        shiftDown = qMax(shiftDown, denominatorGap + rule / 2 - axis + denominator.ascent);

        const qreal width = qMax(numerator.width, denominator.width);
        const qreal pad = sNullDelimiterSpace;

        Box box;
        box.place(numerator, pad + (width - numerator.width) / 2, -shiftUp);
        box.place(denominator, pad + (width - denominator.width) / 2, shiftDown);
        box.addRule(QRectF(pad, -axis - rule / 2, width, rule));
        box.width = width + 2 * pad;
        return box;
    }

    /** A square root (TeX rule 11, OpenType radical constants). */
    Box radicalBox(const Box& content, const Style& style)
    {
        const UBMathFont::Constants& k = constants();
        const qreal s = style.size();
        const qreal rule = k.radicalRuleThickness * s;
        qreal gap = (style.isDisplay() ? k.radicalDisplayStyleVerticalGap : k.radicalVerticalGap) * s;
        const qreal target = content.ascent + content.descent + gap + rule;

        const Box radical = stretchedGlyph(mathFont().glyph(0x221A), target, style);
        const qreal height = radical.ascent + radical.descent;

        if (height > target)
        {
            // TeX: the clearance gets half of the excess
            gap += (height - target) / 2;
        }

        const qreal ruleTop = content.ascent + gap + rule;

        // the sign and its vinculum are one shape: the rule overlaps the end of the sign and
        // runs in the direction of the outline of the sign, so that the overlap stays filled
        const qreal x = radical.width;
        Box sign;
        sign.place(radical, 0, radical.ascent - ruleTop);
        QPainterPath joined;
        joined.setFillRule(Qt::WindingFill);
        qreal orientation = 0;

        for (const QPainterPath& shape : sign.shapes)
        {
            joined.addPath(shape);

            for (const QPolygonF& contour : shape.toSubpathPolygons())
            {
                qreal area = 0;

                for (int i = 0; i < contour.size(); ++i)
                {
                    const QPointF& p = contour.at(i);
                    const QPointF& q = contour.at((i + 1) % contour.size());
                    area += p.x() * q.y() - q.x() * p.y();
                }

                if (qAbs(area) > qAbs(orientation))
                {
                    orientation = area;
                }
            }
        }

        const QRectF bar(x - rule / 4, -ruleTop, content.width + rule / 4, rule);
        QPolygonF barOutline({bar.topLeft(), bar.topRight(), bar.bottomRight(), bar.bottomLeft()});

        if (orientation < 0)
        {
            std::reverse(barOutline.begin(), barOutline.end());
        }

        joined.addPolygon(barOutline);
        joined.closeSubpath();
        sign.shapes = {joined};
        sign.ascent = qMax(sign.ascent, ruleTop);
        sign.width = qMax(sign.width, bar.right());

        Box box;
        box.place(sign, 0, 0);
        box.place(content, x, 0);
        box.ascent = qMax(box.ascent, ruleTop + k.radicalExtraAscender * s);
        return box;
    }

    /** An accent over a box (TeX rule 12), used for the vector arrow. */
    Box accentBox(const Box& base, char32_t accent, const Style& style)
    {
        const quint32 glyph = mathFont().glyph(accent);

        if (!glyph)
        {
            return base;
        }

        const qreal s = style.size();
        const Box mark = stretchedGlyph(glyph, base.width * 0.9, style, false);
        const qreal delta = qMin(base.ascent, constants().accentBaseHeight * s);
        const qreal shiftUp = base.ascent - delta;

        qreal baseAttachment = base.width / 2;

        if (base.character && mathFont().topAccentAttachment(base.glyph) >= 0)
        {
            baseAttachment = mathFont().topAccentAttachment(base.glyph) * base.size;
        }

        QRectF markInk;

        for (const QPainterPath& shape : mark.shapes)
        {
            markInk = markInk.united(shape.boundingRect());
        }

        qreal markAttachment = markInk.center().x();

        if (mathFont().topAccentAttachment(mark.glyph) >= 0 && mark.character)
        {
            markAttachment = mathFont().topAccentAttachment(mark.glyph) * mark.size;
        }

        Box box;
        box.place(base, 0, 0);
        box.place(mark, baseAttachment - markAttachment, -shiftUp);
        box.width = base.width;
        return box;
    }

    /** Superscript and subscript (TeX rule 18, OpenType script constants). */
    Box scriptsBox(const Box& base, const Box* superscript, const Box* subscript, const Style& style)
    {
        const UBMathFont::Constants& k = constants();
        const qreal s = style.size();
        // TeX rule 18a: a character base uses the standard shifts only, any other base (also a
        // big operator, which became a box centred on the axis) drops the scripts from its edges
        qreal shiftUp = base.character ? 0 : base.ascent - k.superscriptBaselineDropMax * s;
        qreal shiftDown = base.character ? 0 : base.descent + k.subscriptBaselineDropMin * s;

        // big operators include the italic correction in their advance
        const qreal italic = base.character || base.largeOperator ? base.italic : 0;
        const qreal superscriptX = base.largeOperator ? base.width : base.width + italic;
        const qreal subscriptX = base.largeOperator ? base.width - italic : base.width;

        if (superscript)
        {
            shiftUp = qMax(shiftUp, (style.cramped ? k.superscriptShiftUpCramped : k.superscriptShiftUp) * s);
            shiftUp = qMax(shiftUp, superscript->descent + k.superscriptBottomMin * s);
        }

        if (subscript)
        {
            shiftDown = qMax(shiftDown, k.subscriptShiftDown * s);

            if (!superscript)
            {
                shiftDown = qMax(shiftDown, subscript->ascent - k.subscriptTopMax * s);
            }
        }

        if (superscript && subscript)
        {
            const qreal gap = (shiftUp - superscript->descent) - (subscript->ascent - shiftDown);

            if (gap < k.subSuperscriptGapMin * s)
            {
                shiftDown += k.subSuperscriptGapMin * s - gap;
                const qreal psi = k.superscriptBottomMaxWithSubscript * s - (shiftUp - superscript->descent);

                if (psi > 0)
                {
                    shiftUp += psi;
                    shiftDown -= psi;
                }
            }
        }

        Box box;
        box.place(base, 0, 0);
        qreal right = base.width;

        if (superscript)
        {
            box.place(*superscript, superscriptX, -shiftUp);
            right = qMax(right, superscriptX + superscript->width);
        }

        if (subscript)
        {
            box.place(*subscript, subscriptX, shiftDown);
            right = qMax(right, subscriptX + subscript->width);
        }

        box.width = right + k.spaceAfterScript * s;
        return box;
    }

    /** Limits above and below a big operator or lim in display style (TeX rule 13a). */
    Box limitsBox(const Box& base, const Box* upper, const Box* lower, const Style& style)
    {
        const UBMathFont::Constants& k = constants();
        const qreal s = style.size();
        const qreal italic = base.italic;
        qreal width = base.width;

        if (upper)
        {
            width = qMax(width, upper->width);
        }

        if (lower)
        {
            width = qMax(width, lower->width);
        }

        Box box;
        box.place(base, (width - base.width) / 2, 0);

        if (upper)
        {
            const qreal rise = qMax(k.upperLimitBaselineRiseMin * s, k.upperLimitGapMin * s + upper->descent);
            box.place(*upper, (width - upper->width + italic) / 2, -base.ascent - rise);
        }

        if (lower)
        {
            const qreal drop = qMax(k.lowerLimitBaselineDropMin * s, k.lowerLimitGapMin * s + lower->ascent);
            box.place(*lower, (width - lower->width - italic) / 2, base.descent + drop);
        }

        box.width = width;
        return box;
    }

    // -------------------------------------------------------------------- characters

    /** Variables and lower case Greek are italic (Unicode mathematical italic letters). */
    char32_t mathItalic(char32_t c)
    {
        char32_t italic = c;

        if (c >= 'a' && c <= 'z')
        {
            italic = c == 'h' ? 0x210E : 0x1D44E + (c - 'a');
        }
        else if (c >= 'A' && c <= 'Z')
        {
            italic = 0x1D434 + (c - 'A');
        }
        else if (c >= 0x3B1 && c <= 0x3C9)
        {
            italic = 0x1D6FC + (c - 0x3B1);
        }
        else
        {
            static const QHash<char32_t, char32_t> variants = {
                {0x3F5, 0x1D716}, {0x3D1, 0x1D717}, {0x3F0, 0x1D718},
                {0x3D5, 0x1D719}, {0x3F1, 0x1D71A}, {0x3D6, 0x1D71B}
            };
            italic = variants.value(c, c);
        }

        return mathFont().glyph(italic) ? italic : c;
    }

    Atom atomOf(char32_t c)
    {
        static const QHash<char32_t, Atom> atoms = []() {
            QHash<char32_t, Atom> t;

            for (char32_t b : {U'+', U'−', U'±', U'∓', U'×', U'÷', U'·', U'⋅',
                               U'∪', U'∩', U'∘', U'∗', U'⊕', U'⊗', U'∧', U'∨', U'∖'})
            {
                t.insert(b, Atom::Bin);
            }

            for (char32_t r : {U'=', U'<', U'>', U':', U'≠', U'≈', U'≡', U'≤', U'≥', U'→',
                               U'←', U'↔', U'⇒', U'⇐', U'⇔', U'↦', U'∈', U'∉',
                               U'∋', U'⊂', U'⊃', U'⊆', U'⊇', U'∼', U'≃', U'≅',
                               U'∝', U'⊥', U'∥', U'≪', U'≫'})
            {
                t.insert(r, Atom::Rel);
            }

            for (char32_t o : {U'(', U'[', U'{', U'⟨', U'⌊', U'⌈'})
            {
                t.insert(o, Atom::Open);
            }

            for (char32_t o : {U')', U']', U'}', U'⟩', U'⌋', U'⌉', U'!'})
            {
                t.insert(o, Atom::Close);
            }

            t.insert(',', Atom::Punct);
            t.insert(';', Atom::Punct);

            for (char32_t o : {U'∑', U'∏', U'∫', U'∬', U'∭', U'∮', U'⋃', U'⋂'})
            {
                t.insert(o, Atom::Op);
            }

            return t;
        }();

        return atoms.value(c, Atom::Ord);
    }

    bool isBigOperator(char32_t c)
    {
        return c == 0x2211 || c == 0x220F || c == 0x222B || c == 0x222C || c == 0x222D || c == 0x222E;
    }

    /** Sums and products take their limits above and below in display style, integrals at the side. */
    bool takesLimits(char32_t c)
    {
        return c == 0x2211 || c == 0x220F;
    }

    // -------------------------------------------------------------------- formula tree

    enum class Kind
    {
        Glyph,          // one character
        Word,           // function name, upright
        Differential,   // d followed by a variable
        Row,            // a sequence
        Fenced,         // a sequence between delimiters that grow with it
        Fraction,
        Scripts,        // base with superscript and/or subscript
        BigOperator,
        Limit,          // lim, max, min
        Radical,
        Vector,
        Placeholder
    };

    struct Node
    {
        Kind kind{Kind::Row};
        char32_t character{0};
        char32_t close{0};
        QString text;
        bool transparentAtoms{false};
        qreal quadsBefore{0};
        std::vector<Node> items;
        std::vector<Node> superscript;
        std::vector<Node> subscript;
    };

    Node glyphNode(char32_t c)
    {
        Node node;
        node.kind = Kind::Glyph;
        node.character = c;
        return node;
    }

    Node rowNode(std::vector<Node> items = {})
    {
        Node node;
        node.kind = Kind::Row;
        node.items = std::move(items);
        return node;
    }

    struct Laid
    {
        Box box;
        Atom left{Atom::Ord};
        Atom right{Atom::Ord};
        bool differential{false};
    };

    Box layoutRow(const std::vector<Node>& items, const Style& style);
    Laid layoutNode(const Node& node, const Style& style);

    Box wordBox(const QString& text, const Style& style)
    {
        Box box;

        for (const QChar c : text)
        {
            box.append(glyphBox(mathFont().glyph(c.unicode()), style));
        }

        return box;
    }

    Box bigOperatorBox(char32_t c, const Style& style)
    {
        const quint32 glyph = mathFont().glyph(c);
        Box op;

        if (style.isDisplay())
        {
            op = stretchedGlyph(glyph, constants().displayOperatorMinHeight, style);
        }
        else
        {
            op = glyphBox(glyph, style);
        }

        Box box = centredOnAxis(op, style);
        box.italic = op.italic;
        box.largeOperator = true;
        return box;
    }

    Laid layoutNode(const Node& node, const Style& style)
    {
        Laid laid;

        switch (node.kind)
        {
        case Kind::Glyph:
            laid.box = glyphBox(mathFont().glyph(node.character), style);
            laid.left = laid.right = atomOf(node.character);
            break;

        case Kind::Word:
            laid.box = wordBox(node.text, style);
            laid.left = laid.right = Atom::Op;
            break;

        case Kind::Differential:
        {
            laid.box = wordBox(node.text.left(1), style);
            laid.box.append(glyphBox(mathFont().glyph(mathItalic(node.text.at(1).unicode())), style));
            laid.differential = true;
            break;
        }

        case Kind::Row:
        {
            laid.box = layoutRow(node.items, style);

            if (node.transparentAtoms && !node.items.empty())
            {
                laid.left = layoutNode(node.items.front(), style).left;
                laid.right = layoutNode(node.items.back(), style).right;
            }

            break;
        }

        case Kind::Fenced:
        {
            const Box inner = layoutRow(node.items, style);
            laid.box = delimiterBox(node.character, inner.ascent, inner.descent, style);
            laid.box.append(inner);

            if (node.close)
            {
                laid.box.append(delimiterBox(node.close, inner.ascent, inner.descent, style));
            }

            laid.left = Atom::Open;
            laid.right = node.close ? Atom::Close : Atom::Ord;
            break;
        }

        case Kind::Fraction:
            if (style.isScript() && node.items.at(0).items.size() == 1 && node.items.at(1).items.size() == 1
                    && node.items.at(0).items.front().kind == Kind::Glyph && node.items.at(1).items.front().kind == Kind::Glyph)
            {
                // a simple fraction in a script is written with a solidus, as a^{1/n}
                laid.box = layoutRow({node.items.at(0).items.front(), glyphNode('/'), node.items.at(1).items.front()}, style);
                break;
            }

            laid.box = fractionBox(layoutRow(node.items.at(0).items, style.numerator()),
                                   layoutRow(node.items.at(1).items, style.denominator()), style);
            laid.left = laid.right = Atom::Inner;
            break;

        case Kind::Scripts:
        {
            const Node& baseNode = node.items.front();
            const Laid base = layoutNode(baseNode, style);
            Box superscript;
            Box subscript;
            const bool hasSuperscript = !node.superscript.empty();
            const bool hasSubscript = !node.subscript.empty();

            if (hasSuperscript)
            {
                superscript = layoutRow(node.superscript.front().items, style.superscript());
            }

            if (hasSubscript)
            {
                subscript = layoutRow(node.subscript.front().items, style.subscript());
            }

            const bool limits = style.isDisplay()
                    && (baseNode.kind == Kind::Limit || (baseNode.kind == Kind::BigOperator && takesLimits(baseNode.character)));

            if (limits)
            {
                laid.box = limitsBox(base.box, hasSuperscript ? &superscript : nullptr, hasSubscript ? &subscript : nullptr, style);
            }
            else
            {
                laid.box = scriptsBox(base.box, hasSuperscript ? &superscript : nullptr, hasSubscript ? &subscript : nullptr, style);
            }

            laid.left = base.left;
            laid.right = base.right;
            break;
        }

        case Kind::BigOperator:
            laid.box = bigOperatorBox(node.character, style);
            laid.left = laid.right = Atom::Op;
            break;

        case Kind::Limit:
            laid.box = wordBox(node.text, style);
            laid.left = laid.right = Atom::Op;
            break;

        case Kind::Radical:
            laid.box = radicalBox(layoutRow(node.items.front().items, style.crampedStyle()), style);
            break;

        case Kind::Vector:
        {
            const Node& operand = node.items.front();
            Box base;

            if (operand.items.size() == 1 && operand.items.front().kind == Kind::Glyph)
            {
                // a single letter: the accent is attached to the glyph itself
                base = layoutNode(operand.items.front(), style.crampedStyle()).box;
            }
            else
            {
                base = layoutRow(operand.items, style.crampedStyle());
            }

            laid.box = accentBox(base, 0x20D7, style);
            break;
        }

        case Kind::Placeholder:
            laid.box = placeholderBox(style);
            break;
        }

        return laid;
    }

    Box layoutRow(const std::vector<Node>& items, const Style& style)
    {
        std::vector<Laid> laid;
        laid.reserve(items.size());

        for (const Node& item : items)
        {
            laid.push_back(layoutNode(item, style));
        }

        // a binary operator without a left operand is an ordinary symbol (TeX rules 5 and 6)
        for (size_t i = 0; i < laid.size(); ++i)
        {
            Laid& current = laid[i];

            if (current.left == Atom::Bin)
            {
                const Atom previous = i == 0 ? Atom::Op : laid[i - 1].right;

                if (previous == Atom::Bin || previous == Atom::Op || previous == Atom::Rel
                        || previous == Atom::Open || previous == Atom::Punct)
                {
                    current.left = current.right = Atom::Ord;
                }
            }

            if (i > 0 && laid[i - 1].right == Atom::Bin
                    && (current.left == Atom::Rel || current.left == Atom::Close || current.left == Atom::Punct))
            {
                laid[i - 1].left = laid[i - 1].right = Atom::Ord;
            }
        }

        if (!laid.empty() && laid.back().right == Atom::Bin)
        {
            laid.back().left = laid.back().right = Atom::Ord;
        }

        Box row;

        for (size_t i = 0; i < laid.size(); ++i)
        {
            Box box = laid[i].box;

            // TeX rule 17: a character keeps its italic correction
            if (box.character && !box.largeOperator)
            {
                box.width += box.italic;
            }

            qreal gap = items[i].quadsBefore * style.size();

            if (i > 0)
            {
                const qreal atomSpace = spaceBetween(laid[i - 1].right, laid[i].left, style);
                gap += atomSpace;

                // a thin space before the d of dx, as \,dx in TeX
                if (laid[i].differential && atomSpace == 0 && !style.isScript()
                        && (laid[i - 1].right == Atom::Ord || laid[i - 1].right == Atom::Close || laid[i - 1].right == Atom::Inner))
                {
                    gap += 3 / 18.0 * style.size();
                }
            }

            row.append(box, gap);
        }

        return row;
    }

    // -------------------------------------------------------------------- vocabulary

    enum class WordKind
    {
        Function,
        Symbol,
        BigOperator,
        Limit,
        SquareRoot,
        Vector
    };

    struct Word
    {
        WordKind kind;
        QString text;
    };

    const QHash<QString, Word>& words()
    {
        static const QHash<QString, Word> table = []() {
            QHash<QString, Word> t;

            const QStringList functions = {
                "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan",
                "sinh", "cosh", "tanh", "ln", "log", "exp", "det", "mod", "gcd", "lcm", "arg", "dim"
            };

            for (const QString& f : functions)
            {
                t.insert(f, {WordKind::Function, f});
            }

            const QList<QPair<QString, QString>> symbols = {
                {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ε"},
                {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"},
                {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
                {"pi", "π"}, {"rho", "ρ"}, {"sigma", "σ"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"},
                {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},
                {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"},
                {"Pi", "Π"}, {"Sigma", "Σ"}, {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
                {"inf", "∞"}, {"infty", "∞"}, {"infinity", "∞"}, {"partial", "∂"}, {"nabla", "∇"},
                {"pm", "±"}, {"mp", "∓"}, {"times", "×"}, {"div", "÷"}, {"cdot", "⋅"}, {"le", "≤"}, {"leq", "≤"},
                {"ge", "≥"}, {"geq", "≥"}, {"ne", "≠"}, {"neq", "≠"}, {"approx", "≈"}, {"equiv", "≡"},
                {"in", "∈"}, {"notin", "∉"}, {"subset", "⊂"}, {"supset", "⊃"}, {"subseteq", "⊆"},
                {"supseteq", "⊇"}, {"cup", "∪"}, {"cap", "∩"}, {"empty", "∅"}, {"emptyset", "∅"},
                {"forall", "∀"}, {"exists", "∃"}, {"deg", "°"}, {"perp", "⊥"}, {"parallel", "∥"},
                {"angle", "∠"}, {"to", "→"}, {"circ", "∘"},
                {"NN", "ℕ"}, {"ZZ", "ℤ"}, {"QQ", "ℚ"}, {"RR", "ℝ"}, {"CC", "ℂ"}
            };

            for (const auto& symbol : symbols)
            {
                t.insert(symbol.first, {WordKind::Symbol, symbol.second});
            }

            t.insert("int", {WordKind::BigOperator, "∫"});
            t.insert("iint", {WordKind::BigOperator, "∬"});
            t.insert("oint", {WordKind::BigOperator, "∮"});
            t.insert("sum", {WordKind::BigOperator, "∑"});
            t.insert("prod", {WordKind::BigOperator, "∏"});
            t.insert("lim", {WordKind::Limit, "lim"});
            t.insert("max", {WordKind::Limit, "max"});
            t.insert("min", {WordKind::Limit, "min"});
            t.insert("sqrt", {WordKind::SquareRoot, QString()});
            t.insert("vec", {WordKind::Vector, QString()});
            return t;
        }();

        return table;
    }

    /** Multi-character ASCII operators, longest first. */
    const QList<QPair<QString, char32_t>>& asciiOperators()
    {
        static const QList<QPair<QString, char32_t>> table = {
            {"<=>", 0x21D4}, {"->", 0x2192}, {"<-", 0x2190}, {"=>", 0x21D2},
            {"<=", 0x2264}, {">=", 0x2265}, {"!=", 0x2260}, {"~~", 0x2248},
            {"+-", 0x00B1}, {"//", '/'}, {"*", 0x22C5}, {"-", 0x2212}, {"~", 0x223C}
        };
        return table;
    }

    QChar superscriptDigit(QChar c)
    {
        switch (c.unicode())
        {
        case 0x00B2: return '2';
        case 0x00B3: return '3';
        case 0x00B9: return '1';
        case 0x2070: return '0';
        case 0x2074: return '4';
        case 0x2075: return '5';
        case 0x2076: return '6';
        case 0x2077: return '7';
        case 0x2078: return '8';
        case 0x2079: return '9';
        case 0x207F: return 'n';
        default: return QChar();
        }
    }

    // -------------------------------------------------------------------- parser

    class Parser
    {
        public:
            explicit Parser(const QString& source)
                : mSource(source)
            {
            }

            Node parse(QString* error)
            {
                Node row = parseSequence(QChar());

                while (mError.isEmpty() && mPos < mSource.size())
                {
                    // stray closing bracket: write it and continue
                    Node bracket;

                    if (glyphItem(mSource.at(mPos++).unicode(), bracket))
                    {
                        row.items.push_back(bracket);
                        Node rest = parseSequence(QChar());
                        row.items.insert(row.items.end(), rest.items.begin(), rest.items.end());
                    }
                }

                if (error)
                {
                    *error = mError;
                }

                return row;
            }

        private:
            /** A parenthesised group used as an operand loses its parentheses: (x+1)/2. */
            static Node operand(const Node& item)
            {
                if (item.kind == Kind::Fenced && item.close && (item.character == '(' || item.character == '['))
                {
                    return rowNode(item.items);
                }

                return item.kind == Kind::Row ? item : rowNode({item});
            }

            void skipSpaces()
            {
                while (mPos < mSource.size() && mSource.at(mPos).isSpace())
                {
                    ++mPos;
                }
            }

            QChar peek()
            {
                skipSpaces();
                return mPos < mSource.size() ? mSource.at(mPos) : QChar();
            }

            bool lookingAt(const QString& text)
            {
                skipSpaces();
                return QStringView(mSource).mid(mPos).startsWith(text);
            }

            Node parseSequence(QChar close)
            {
                Node row = rowNode();

                while (mError.isEmpty())
                {
                    const QChar c = peek();

                    if (c.isNull() || c == close || (close.isNull() && (c == ')' || c == ']' || c == '}')))
                    {
                        break;
                    }

                    if (close != QChar() && (c == ')' || c == ']' || c == '}') && c != close)
                    {
                        // mismatched bracket inside a group: let the group end here
                        break;
                    }

                    // spaces are ignored as in TeX, but two or more typed spaces are a \quad
                    int spaces = 0;

                    while (mPos - spaces - 1 >= 0 && mSource.at(mPos - spaces - 1) == ' ')
                    {
                        ++spaces;
                    }

                    Node item;

                    if (!parseItem(item))
                    {
                        break;
                    }

                    if (spaces >= 2 && !row.items.empty())
                    {
                        item.quadsBefore = 1;
                    }

                    // fractions bind tighter than anything else in a sequence
                    while (mError.isEmpty() && peek() == '/' && !lookingAt("//"))
                    {
                        ++mPos;
                        Node denominator;

                        if (!parseItem(denominator))
                        {
                            denominator.kind = Kind::Placeholder;
                        }

                        Node fraction;
                        fraction.kind = Kind::Fraction;
                        fraction.items = {operand(item), operand(denominator)};
                        item = fraction;
                    }

                    row.items.push_back(item);
                }

                return row;
            }

            /** Parse one primary with its primes and scripts. Returns false at the end of the input. */
            bool parseItem(Node& item)
            {
                if (!parsePrimary(item))
                {
                    return false;
                }

                int primes = 0;
                bool hasSuperscript = false;
                bool hasSubscript = false;
                Node superscript = rowNode();
                Node subscript = rowNode();

                while (mError.isEmpty())
                {
                    const QChar c = mPos < mSource.size() ? mSource.at(mPos) : QChar();

                    if ((c == '\'' || c == QChar(0x2032)) && item.kind != Kind::BigOperator && item.kind != Kind::Limit)
                    {
                        ++mPos;
                        ++primes;
                    }
                    else if (peek() == '^' || peek() == '_')
                    {
                        const QChar op = mSource.at(mPos++);
                        Node script = parseScriptOperand();

                        if (op == '^')
                        {
                            superscript = script;
                            hasSuperscript = true;
                        }
                        else
                        {
                            subscript = script;
                            hasSubscript = true;
                        }
                    }
                    else if (!superscriptDigit(peek()).isNull())
                    {
                        superscript.items.push_back(glyphNode(superscriptDigit(mSource.at(mPos++)).unicode()));
                        hasSuperscript = true;
                    }
                    else
                    {
                        break;
                    }
                }

                if (primes > 0)
                {
                    // f' is f^{\prime}, TeX style
                    std::vector<Node> marks(primes, glyphNode(0x2032));
                    superscript.items.insert(superscript.items.begin(), marks.begin(), marks.end());
                    hasSuperscript = true;
                }

                if (hasSuperscript || hasSubscript)
                {
                    Node scripts;
                    scripts.kind = Kind::Scripts;
                    scripts.items = {item};

                    if (hasSuperscript)
                    {
                        scripts.superscript = {superscript};
                    }

                    if (hasSubscript)
                    {
                        scripts.subscript = {subscript};
                    }

                    item = scripts;
                }

                return true;
            }

            /** The operand of ^ or _: one primary (a group loses its parentheses), with an optional sign. */
            Node parseScriptOperand()
            {
                Node result = rowNode();
                const QChar c = peek();

                if (c == '-' || c == '+' || c == QChar(0x2212))
                {
                    ++mPos;
                    result.items.push_back(glyphNode(c == '+' ? '+' : 0x2212));
                }

                Node primary;

                if (!parsePrimary(primary))
                {
                    if (result.items.empty())
                    {
                        Node placeholder;
                        placeholder.kind = Kind::Placeholder;
                        result.items.push_back(placeholder);
                    }

                    return result;
                }

                const Node inner = operand(primary);
                result.items.insert(result.items.end(), inner.items.begin(), inner.items.end());
                return result;
            }

            bool parsePrimary(Node& item)
            {
                const QChar c = peek();

                if (c.isNull())
                {
                    return false;
                }

                // groups
                if (c == '(' || c == '[' || c == '{')
                {
                    const QChar close = c == '(' ? ')' : (c == '[' ? ']' : '}');
                    ++mPos;
                    const Node inner = parseSequence(close);
                    bool closed = false;

                    if (peek() == close)
                    {
                        ++mPos;
                        closed = true;
                    }

                    if (c == '{')
                    {
                        item = inner;
                    }
                    else
                    {
                        // an opening bracket without its closing one is written alone
                        item.kind = Kind::Fenced;
                        item.character = c.unicode();
                        item.close = closed ? close.unicode() : 0;
                        item.items = inner.items;
                    }

                    return true;
                }

                // escaped characters: literal braces and other specials
                if (c == '\\' && mPos + 1 < mSource.size())
                {
                    const char32_t escaped = mSource.at(mPos + 1).unicode();
                    mPos += 2;
                    return glyphItem(escaped, item);
                }

                if (c == '#' || c == QChar(0x25A1))
                {
                    ++mPos;
                    item.kind = Kind::Placeholder;
                    return true;
                }

                if (isAsciiLetter(c))
                {
                    return wordItem(item);
                }

                for (const auto& op : asciiOperators())
                {
                    if (lookingAt(op.first))
                    {
                        mPos += op.first.size();
                        return glyphItem(op.second, item);
                    }
                }

                // characters typed directly
                char32_t code = c.unicode();
                int length = 1;

                if (c.isHighSurrogate() && mPos + 1 < mSource.size() && mSource.at(mPos + 1).isLowSurrogate())
                {
                    code = QChar::surrogateToUcs4(c, mSource.at(mPos + 1));
                    length = 2;
                }

                mPos += length;

                if (code == 0x221A)
                {
                    return radicalItem(item);
                }

                if (isBigOperator(code))
                {
                    item.kind = Kind::BigOperator;
                    item.character = code;
                    return true;
                }

                return glyphItem(code, item);
            }

            bool glyphItem(char32_t c, Node& item)
            {
                // the middle dot of the keyboard is the multiplication dot of maths
                if (c == 0x00B7)
                {
                    c = 0x22C5;
                }

                const char32_t styled = mathItalic(c);

                if (!mathFont().glyph(styled))
                {
                    mError = QCoreApplication::translate("UBMathInkLayout", "Cannot write the character \"%1\".")
                            .arg(QString::fromUcs4(&c, 1));
                    return false;
                }

                item = glyphNode(styled);
                return true;
            }

            /**
             * A function name or a letter directly followed by a parenthesised group, like f(x)
             * or sin(x), is a single item: f(x)/2 is the fraction of f(x) and 2.
             */
            bool attachArguments(Node& item)
            {
                // f'(x): the primes belong to the function
                int end = mPos;

                while (end < mSource.size() && (mSource.at(end) == '\'' || mSource.at(end) == QChar(0x2032)))
                {
                    ++end;
                }

                if (end > mPos && end < mSource.size() && mSource.at(end) == '(')
                {
                    Node scripts;
                    scripts.kind = Kind::Scripts;
                    scripts.items = {item};
                    scripts.superscript = {rowNode(std::vector<Node>(end - mPos, glyphNode(0x2032)))};
                    item = scripts;
                    mPos = end;
                }

                if (mPos < mSource.size() && mSource.at(mPos) == '(')
                {
                    Node arguments;

                    if (parsePrimary(arguments))
                    {
                        Node row = rowNode({item, arguments});
                        row.transparentAtoms = true;
                        item = row;
                    }
                }

                return mError.isEmpty();
            }

            bool radicalItem(Node& item)
            {
                Node content;

                if (!parsePrimary(content))
                {
                    content.kind = Kind::Placeholder;
                }

                item.kind = Kind::Radical;
                item.items = {operand(content)};
                return true;
            }

            static bool isAsciiLetter(QChar c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }

            bool wordItem(Node& item)
            {
                int end = mPos;

                while (end < mSource.size() && isAsciiLetter(mSource.at(end)))
                {
                    ++end;
                }

                const QString run = mSource.mid(mPos, end - mPos);
                const bool atWordStart = mPos == 0 || !isAsciiLetter(mSource.at(mPos - 1));
                QString word;

                if (atWordStart && words().contains(run))
                {
                    word = run;
                }
                else
                {
                    // the longest known word of at least three letters at the start of the run
                    for (int length = run.size(); length >= 3; --length)
                    {
                        if (words().contains(run.left(length)))
                        {
                            word = run.left(length);
                            break;
                        }
                    }
                }

                if (word.isEmpty() && atWordStart && run.size() == 2 && run.at(0) == 'd')
                {
                    // a differential (dx, dt, ...) is one item with an upright d (ISO 80000-2),
                    // so that d/dx and dy/dx are fractions of differentials
                    mPos += 2;
                    item.kind = Kind::Differential;
                    item.text = run;
                    return true;
                }

                if (word.isEmpty())
                {
                    ++mPos;

                    if (run.size() == 1 && run.at(0) == 'd' && peek() == '/')
                    {
                        // the operator d in d/dx is upright as well
                        item.kind = Kind::Word;
                        item.text = run;
                        item = rowNode({item});
                        return true;
                    }

                    return glyphItem(run.at(0).unicode(), item) && attachArguments(item);
                }

                mPos += word.size();
                const Word& w = words().value(word);

                switch (w.kind)
                {
                case WordKind::Function:
                    item.kind = Kind::Word;
                    item.text = w.text;
                    return attachArguments(item);

                case WordKind::Symbol:
                    return glyphItem(w.text.toUcs4().value(0), item);

                case WordKind::BigOperator:
                    item.kind = Kind::BigOperator;
                    item.character = w.text.toUcs4().value(0);
                    return true;

                case WordKind::Limit:
                    item.kind = Kind::Limit;
                    item.text = w.text;
                    return true;

                case WordKind::SquareRoot:
                    return radicalItem(item);

                case WordKind::Vector:
                {
                    Node content;

                    if (!parsePrimary(content))
                    {
                        content.kind = Kind::Placeholder;
                    }

                    item.kind = Kind::Vector;
                    item.items = {operand(content)};
                    return true;
                }
                }

                return false;
            }

            QString mSource;
            int mPos{0};
            QString mError;
    };
}

// ------------------------------------------------------------------------ UBMathInkLayout

QRectF UBMathInkLayout::Formula::bounds() const
{
    QRectF rect;

    for (const QPainterPath& shape : shapes)
    {
        rect = rect.isNull() ? shape.boundingRect() : rect.united(shape.boundingRect());
    }

    for (const QRectF& placeholder : placeholders)
    {
        rect = rect.isNull() ? placeholder : rect.united(placeholder);
    }

    return rect;
}

QPainterPath UBMathInkLayout::Formula::path() const
{
    QPainterPath all;
    all.setFillRule(Qt::WindingFill);

    for (const QPainterPath& shape : shapes)
    {
        all.addPath(shape);
    }

    return all;
}

bool UBMathInkLayout::isAvailable()
{
    return UBMathFont::font().isValid();
}

UBMathInkLayout::Formula UBMathInkLayout::layout(const QString& source, QString* error, bool displayStyle)
{
    Formula formula;
    formula.source = source;

    if (!isAvailable())
    {
        if (error)
        {
            *error = QCoreApplication::translate("UBMathInkLayout", "The maths font could not be loaded.");
        }

        return formula;
    }

    Parser parser(source);
    QString parseError;
    const Node root = parser.parse(&parseError);

    if (error)
    {
        *error = parseError;
    }

    if (!parseError.isEmpty())
    {
        return formula;
    }

    const Box box = layoutRow(root.items, Style{displayStyle ? 0 : 1, false});
    formula.shapes = box.shapes;
    formula.placeholders = box.placeholders;

    if (formula.shapes.isEmpty() && formula.placeholders.isEmpty() && error && error->isEmpty())
    {
        *error = QCoreApplication::translate("UBMathInkLayout", "Nothing to write.");
    }

    return formula;
}
