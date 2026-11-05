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

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QTransform>
#include <QtMath>

#include "core/memcheck.h"

namespace
{
    // -------------------------------------------------------------------- glyph library

    struct GlyphDef
    {
        qreal advance{0};
        QList<QPolygonF> strokes;
        QRectF bounds;
    };

    struct Library
    {
        bool loaded{false};
        qreal axis{0.23};
        QHash<QChar, GlyphDef> glyphs;
    };

    Library loadLibrary()
    {
        Library lib;
        QFile file(":/math/inkGlyphs.json");

        if (!file.open(QIODevice::ReadOnly))
        {
            qWarning() << "UBMathInkLayout: cannot open the glyph library" << file.fileName();
            return lib;
        }

        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject metrics = root.value("metrics").toObject();
        lib.axis = metrics.value("axis").toDouble(0.23);

        const QJsonObject glyphs = root.value("glyphs").toObject();

        for (auto it = glyphs.constBegin(); it != glyphs.constEnd(); ++it)
        {
            const QString key = it.key();

            if (key.size() != 1)
            {
                continue;
            }

            const QJsonObject glyph = it.value().toObject();
            GlyphDef def;
            def.advance = glyph.value("w").toDouble();

            for (const QJsonValue& strokeValue : glyph.value("s").toArray())
            {
                const QJsonArray coordinates = strokeValue.toArray();
                QPolygonF stroke;

                for (int i = 0; i + 1 < coordinates.size(); i += 2)
                {
                    stroke << QPointF(coordinates.at(i).toDouble(), coordinates.at(i + 1).toDouble());
                }

                if (stroke.size() >= 2)
                {
                    def.strokes << stroke;
                    def.bounds = def.bounds.isNull() ? stroke.boundingRect() : def.bounds.united(stroke.boundingRect());
                }
            }

            lib.glyphs.insert(key.at(0), def);
        }

        lib.loaded = !lib.glyphs.isEmpty();
        return lib;
    }

    const Library& library()
    {
        static const Library lib = loadLibrary();
        return lib;
    }

    // -------------------------------------------------------------------- boxes

    const qreal sPlaceholderWidth = 0.6;
    const qreal sScriptScale = 0.62;
    const qreal sOperatorPadding = 0.08;

    /** A laid out piece of a formula. Coordinates in em, y axis down, origin at the left end of the baseline. */
    struct Box
    {
        qreal width{0};
        qreal ascent{0};
        qreal descent{0};
        QList<UBMathInkLayout::Glyph> glyphs;
        QList<QRectF> placeholders;

        qreal height() const { return ascent + descent; }

        /** Place another box with its baseline origin at (x, y), scaled. */
        void place(const Box& other, qreal x, qreal y, qreal scale = 1.0)
        {
            QTransform transform;
            transform.translate(x, y);
            transform.scale(scale, scale);

            for (const UBMathInkLayout::Glyph& glyph : other.glyphs)
            {
                UBMathInkLayout::Glyph copy;

                for (const QPolygonF& stroke : glyph.strokes)
                {
                    copy.strokes << transform.map(stroke);
                }

                copy.box = transform.mapRect(glyph.box);
                glyphs << copy;
            }

            for (const QRectF& rect : other.placeholders)
            {
                placeholders << transform.mapRect(rect);
            }

            width = qMax(width, x + other.width * scale);
            ascent = qMax(ascent, other.ascent * scale - y);
            descent = qMax(descent, other.descent * scale + y);
        }

        /** Append another box on the right, on the same baseline. */
        void append(const Box& other, qreal gap = 0)
        {
            place(other, width + gap, 0);
        }

        /** Add a structural stroke (fraction bar, vinculum, arrow) as its own symbol. */
        void addStrokes(const QList<QPolygonF>& strokes)
        {
            UBMathInkLayout::Glyph glyph;
            glyph.strokes = strokes;

            for (const QPolygonF& stroke : strokes)
            {
                glyph.box = glyph.box.isNull() ? stroke.boundingRect() : glyph.box.united(stroke.boundingRect());
            }

            glyphs << glyph;
            ascent = qMax(ascent, -glyph.box.top());
            descent = qMax(descent, glyph.box.bottom());
            width = qMax(width, glyph.box.right());
        }
    };

    Box glyphBox(QChar c, const QTransform& extra = QTransform())
    {
        Box box;
        const auto it = library().glyphs.constFind(c);

        if (it == library().glyphs.constEnd())
        {
            return box;
        }

        UBMathInkLayout::Glyph glyph;

        for (const QPolygonF& stroke : it->strokes)
        {
            glyph.strokes << extra.map(stroke);
        }

        glyph.box = extra.mapRect(it->bounds);
        box.glyphs << glyph;
        box.width = it->advance;
        box.ascent = qMax(0., -glyph.box.top());
        box.descent = qMax(0., glyph.box.bottom());
        return box;
    }

    Box textBox(const QString& text)
    {
        Box box;

        for (const QChar c : text)
        {
            box.append(glyphBox(c));
        }

        return box;
    }

    Box placeholderBox()
    {
        Box box;
        box.width = sPlaceholderWidth;
        box.ascent = 0.6;
        box.descent = 0.04;
        box.placeholders << QRectF(0.05, -0.6, sPlaceholderWidth - 0.1, 0.64);
        return box;
    }

    Box spaceBox(qreal width)
    {
        Box box;
        box.width = width;
        return box;
    }

    /** A fence ( ) [ ] | { } stretched vertically to enclose content of the given extent. */
    Box fenceBox(QChar c, qreal ascent, qreal descent)
    {
        const auto it = library().glyphs.constFind(c);

        if (it == library().glyphs.constEnd())
        {
            return Box();
        }

        const qreal top = -(ascent + 0.06);
        const qreal bottom = descent + 0.06;
        const QRectF bounds = it->bounds;
        QTransform transform;

        if (top < bounds.top() || bottom > bounds.bottom())
        {
            const qreal newTop = qMin(top, bounds.top());
            const qreal newBottom = qMax(bottom, bounds.bottom());
            const qreal sy = (newBottom - newTop) / bounds.height();
            transform.translate(0, newTop);
            transform.scale(1.0, sy);
            transform.translate(0, -bounds.top());
        }

        return glyphBox(c, transform);
    }

    Box fractionBox(const Box& numerator, const Box& denominator)
    {
        const qreal scale = 0.9;
        const qreal axis = library().axis;
        const qreal gap = 0.1;
        const qreal side = 0.05;
        const qreal barWidth = qMax(numerator.width, denominator.width) * scale + 0.12;

        Box box;
        box.place(numerator, side + (barWidth - numerator.width * scale) / 2,
                  -axis - gap - numerator.descent * scale, scale);
        box.place(denominator, side + (barWidth - denominator.width * scale) / 2,
                  -axis + gap + denominator.ascent * scale, scale);
        box.addStrokes({QPolygonF({QPointF(side, -axis), QPointF(side + barWidth, -axis)})});
        box.width = barWidth + 2 * side;
        return box;
    }

    Box sqrtBox(const Box& content)
    {
        const qreal top = qMax(content.ascent, 0.42) + 0.1;
        const qreal bottom = qMax(content.descent, 0.0);
        const qreal height = top + bottom;
        const qreal tickY = bottom - qMin(height * 0.42, 0.3);

        QPolygonF radical;
        radical << QPointF(0.02, tickY)
                << QPointF(0.09, tickY - 0.05)
                << QPointF(0.19, bottom);
        const qreal riseX = 0.19 + qMax(0.14, 0.24 * height);
        radical << QPointF(riseX, -top);

        const qreal contentX = riseX + 0.05;
        radical << QPointF(contentX + content.width + 0.05, -top);

        Box box;
        box.place(content, contentX, 0);
        box.addStrokes({radical});
        box.width = contentX + content.width + 0.1;
        return box;
    }

    Box vectorBox(const Box& content)
    {
        const qreal y = -(qMax(content.ascent, 0.42) + 0.1);
        const qreal start = 0.02;
        const qreal end = qMax(content.width, 0.32) - 0.02;
        const qreal head = 0.07;

        Box box;
        box.place(content, 0, 0);
        box.addStrokes({QPolygonF({QPointF(start, y), QPointF(end, y)}),
                        QPolygonF({QPointF(end - head, y - head * 0.7), QPointF(end, y), QPointF(end - head, y + head * 0.7)})});
        box.width = qMax(content.width, 0.32);
        return box;
    }

    Box scriptsBox(const Box& base, const Box* superscript, const Box* subscript)
    {
        Box box = base;
        const qreal x = base.width - 0.01;

        if (superscript)
        {
            const qreal shift = qMax(0.3, base.ascent * 0.72);
            box.place(*superscript, x, -shift, sScriptScale);
        }

        if (subscript)
        {
            const qreal shift = qMax(0.13, base.descent * 0.5);
            box.place(*subscript, x, shift, sScriptScale);
        }

        box.width += 0.03;
        return box;
    }

    enum class BigOpKind
    {
        Integral,
        Sum
    };

    /** A big operator (integral, sum, product) with optional bounds. */
    Box bigOperatorBox(QChar op, BigOpKind kind, const Box* lower, const Box* upper)
    {
        const qreal axis = library().axis;
        const qreal scale = kind == BigOpKind::Integral ? 1.3 : 1.15;
        const Box symbol = glyphBox(op);

        if (symbol.glyphs.isEmpty())
        {
            return Box();
        }

        // centre the symbol on the maths axis
        const QRectF bounds = symbol.glyphs.first().box;
        const qreal dy = -axis - bounds.center().y() * scale;

        Box opBox;
        opBox.place(symbol, 0, dy, scale);
        const qreal opTop = -opBox.ascent;
        const qreal opBottom = opBox.descent;
        const qreal opWidth = opBox.width;

        Box box;

        if (kind == BigOpKind::Integral)
        {
            box.place(opBox, 0, 0);

            if (upper)
            {
                box.place(*upper, opWidth * 0.82, opTop + upper->ascent * sScriptScale, sScriptScale);
            }

            if (lower)
            {
                box.place(*lower, opWidth * 0.42, opBottom + 0.02, sScriptScale);
            }
        }
        else
        {
            qreal width = opWidth;

            if (upper)
            {
                width = qMax(width, upper->width * sScriptScale);
            }

            if (lower)
            {
                width = qMax(width, lower->width * sScriptScale);
            }

            box.place(opBox, (width - opWidth) / 2, 0);

            if (upper)
            {
                box.place(*upper, (width - upper->width * sScriptScale) / 2,
                          opTop - 0.06 - upper->descent * sScriptScale, sScriptScale);
            }

            if (lower)
            {
                box.place(*lower, (width - lower->width * sScriptScale) / 2,
                          opBottom + 0.06 + lower->ascent * sScriptScale, sScriptScale);
            }
        }

        box.width += 0.06;
        return box;
    }

    Box limitBox(const Box& below)
    {
        Box box = textBox("lim");
        const qreal width = qMax(box.width, below.width * sScriptScale);
        Box result;
        result.place(box, (width - box.width) / 2, 0);
        result.place(below, (width - below.width * sScriptScale) / 2,
                     0.08 + below.ascent * sScriptScale, sScriptScale);
        result.width = width + 0.08;
        return result;
    }

    // -------------------------------------------------------------------- vocabulary

    enum class WordKind
    {
        Function,   // written as letters, followed by a small space
        Symbol,     // a single glyph
        BigOperator,
        Limit,
        SquareRoot,
        Vector
    };

    struct Word
    {
        WordKind kind;
        QString glyphs;
    };

    const QHash<QString, Word>& words()
    {
        static const QHash<QString, Word> table = []() {
            QHash<QString, Word> t;

            const QStringList functions = {
                "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan",
                "sinh", "cosh", "tanh", "ln", "log", "exp", "max", "min", "det", "mod", "gcd", "lcm"
            };

            for (const QString& f : functions)
            {
                t.insert(f, {WordKind::Function, f});
            }

            const QList<QPair<QString, QString>> symbols = {
                {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ε"},
                {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"kappa", "κ"}, {"lambda", "λ"},
                {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"}, {"pi", "π"}, {"rho", "ρ"}, {"sigma", "σ"},
                {"tau", "τ"}, {"phi", "φ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},
                {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"},
                {"Pi", "Π"}, {"Sigma", "Σ"}, {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
                {"inf", "∞"}, {"infty", "∞"}, {"infinity", "∞"}, {"partial", "∂"}, {"nabla", "∇"},
                {"pm", "±"}, {"times", "×"}, {"div", "÷"}, {"cdot", "·"}, {"le", "≤"}, {"leq", "≤"},
                {"ge", "≥"}, {"geq", "≥"}, {"ne", "≠"}, {"neq", "≠"}, {"approx", "≈"}, {"equiv", "≡"},
                {"in", "∈"}, {"notin", "∉"}, {"subset", "⊂"}, {"supset", "⊃"}, {"cup", "∪"},
                {"cap", "∩"}, {"empty", "∅"}, {"emptyset", "∅"}, {"forall", "∀"}, {"exists", "∃"},
                {"deg", "°"}, {"perp", "⊥"}, {"parallel", "∥"}, {"angle", "∠"},
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
            t.insert("sqrt", {WordKind::SquareRoot, "√"});
            t.insert("vec", {WordKind::Vector, QString()});
            return t;
        }();

        return table;
    }

    /** Relations and binary operators get some room on both sides. */
    bool isSpacedOperator(QChar c)
    {
        static const QString operators = QStringLiteral("+−±×÷·=≠≈≡<>≤≥→←⇒⇔∈∉⊂⊃∪∩~");
        return operators.contains(c);
    }

    /** Multi-character ASCII operators, longest first. */
    const QList<QPair<QString, QChar>>& asciiOperators()
    {
        static const QList<QPair<QString, QChar>> table = {
            {"<=>", QChar(0x21D4)}, {"->", QChar(0x2192)}, {"=>", QChar(0x21D2)},
            {"<=", QChar(0x2264)}, {">=", QChar(0x2265)}, {"!=", QChar(0x2260)}, {"~~", QChar(0x2248)},
            {"+-", QChar(0x00B1)}, {"//", QChar('/')}, {"*", QChar(0x00B7)}, {"-", QChar(0x2212)}
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

            Box parse(QString* error)
            {
                Box box = parseSequence(QChar());

                while (mError.isEmpty() && mPos < mSource.size())
                {
                    // stray closing bracket: write it and continue
                    box.append(glyphBox(mSource.at(mPos++)));
                    box.append(parseSequence(QChar()));
                }

                if (error)
                {
                    *error = mError;
                }

                return mError.isEmpty() ? box : Box();
            }

        private:
            /** An item: its box, and for a parenthesised group the box without the parentheses. */
            struct Item
            {
                Box box;
                Box inner;
                bool group{false};
                bool bigOperator{false};
                bool limit{false};
                QChar op;

                const Box& operand() const { return group ? inner : box; }
            };

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

            Box parseSequence(QChar close)
            {
                Box box;
                bool previousWasOperator = true;

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

                    // a space typed between two operands is kept as a small gap
                    const bool spaceBefore = mPos > 0 && mSource.at(mPos - 1).isSpace();

                    Item item;

                    if (!parseItem(item))
                    {
                        break;
                    }

                    const bool isOperator = !item.op.isNull() && (isSpacedOperator(item.op) || item.op == ',' || item.op == ';');

                    if (spaceBefore && box.width > 0 && !isOperator && !previousWasOperator)
                    {
                        box.width += 0.16;
                    }

                    previousWasOperator = isOperator;

                    // fractions bind tighter than anything else in a sequence
                    while (mError.isEmpty() && peek() == '/' && !lookingAt("//"))
                    {
                        ++mPos;
                        Item denominator;

                        if (!parseItem(denominator))
                        {
                            denominator.box = placeholderBox();
                        }

                        Item fraction;
                        fraction.box = fractionBox(item.operand(), denominator.operand());
                        item = fraction;
                    }

                    if (!item.op.isNull() && isSpacedOperator(item.op))
                    {
                        box.append(item.box, box.width > 0 ? sOperatorPadding : 0);
                        box.width += sOperatorPadding;
                    }
                    else if (item.op == ',' || item.op == ';')
                    {
                        box.append(item.box);
                        box.width += 0.1;
                    }
                    else
                    {
                        box.append(item.box);
                    }
                }

                return box;
            }

            /** Parse one primary with its postfix scripts. Returns false at the end of the input. */
            bool parseItem(Item& item)
            {
                if (!parsePrimary(item))
                {
                    return false;
                }

                const Box* lower = nullptr;
                const Box* upper = nullptr;
                Box lowerBox;
                Box upperBox;

                while (mError.isEmpty())
                {
                    const QChar c = peek();

                    if (c == '\'' && !item.bigOperator && !item.limit)
                    {
                        ++mPos;
                        Box prime = glyphBox('\'');
                        item.box.place(prime, item.box.width - 0.04, 0);
                        item.group = false;
                    }
                    else if (c == '^' || c == '_')
                    {
                        ++mPos;
                        Box script = parseScriptOperand();

                        if (c == '^')
                        {
                            upperBox = script;
                            upper = &upperBox;
                        }
                        else
                        {
                            lowerBox = script;
                            lower = &lowerBox;
                        }
                    }
                    else if (!superscriptDigit(c).isNull())
                    {
                        ++mPos;
                        upperBox = upper ? upperBox : Box();
                        upperBox.append(glyphBox(superscriptDigit(c)));
                        upper = &upperBox;
                    }
                    else
                    {
                        break;
                    }
                }

                if (item.bigOperator)
                {
                    item.box = bigOperatorBox(item.op, item.op == QChar(0x2211) || item.op == QChar(0x220F)
                                              ? BigOpKind::Sum : BigOpKind::Integral, lower, upper);
                    item.group = false;
                    item.op = QChar();
                }
                else if (item.limit)
                {
                    item.box = limitBox(lower ? *lower : Box());

                    if (upper)
                    {
                        item.box = scriptsBox(item.box, upper, nullptr);
                    }

                    item.group = false;
                }
                else if (lower || upper)
                {
                    item.box = scriptsBox(item.box, upper, lower);
                    item.group = false;
                    item.op = QChar();
                }

                return true;
            }

            /** The operand of ^ or _: one primary (a group loses its parentheses), with an optional sign. */
            Box parseScriptOperand()
            {
                Box sign;
                const QChar c = peek();

                if (c == '-' || c == '+' || c == QChar(0x2212))
                {
                    ++mPos;
                    sign = glyphBox(c == '+' ? QChar('+') : QChar(0x2212));
                }

                Item operand;

                if (!parsePrimary(operand))
                {
                    operand.box = sign.glyphs.isEmpty() ? placeholderBox() : Box();
                }

                // allow a prime inside the script, e.g. f^'
                Box result = sign;
                result.append(operand.operand());
                return result;
            }

            bool parsePrimary(Item& item)
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
                    item.inner = parseSequence(close);
                    item.group = true;

                    if (peek() == close)
                    {
                        ++mPos;
                    }

                    if (c == '{')
                    {
                        item.box = item.inner;
                    }
                    else
                    {
                        const qreal ascent = qMax(item.inner.ascent, 0.42);
                        const qreal descent = item.inner.descent;
                        item.box = fenceBox(c, ascent, descent);
                        item.box.append(item.inner, 0.02);
                        item.box.append(fenceBox(close, ascent, descent), 0.02);
                    }

                    return true;
                }

                // escaped characters: literal braces and other specials
                if (c == '\\' && mPos + 1 < mSource.size())
                {
                    const QChar escaped = mSource.at(mPos + 1);
                    mPos += 2;
                    return glyphItem(escaped, item);
                }

                if (c == '#' || c == QChar(0x25A1))
                {
                    ++mPos;
                    item.box = placeholderBox();
                    return true;
                }

                // ASCII letters: words or single letters
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

                ++mPos;
                return glyphItem(c, item);
            }

            bool glyphItem(QChar c, Item& item)
            {
                if (!library().glyphs.contains(c))
                {
                    mError = QCoreApplication::translate("UBMathInkLayout", "Cannot write the character \"%1\".").arg(c);
                    return false;
                }

                item.box = glyphBox(c);
                item.op = c;
                return true;
            }

            /**
             * A function name or a letter directly followed by a parenthesised group, like f(x)
             * or sin(x), is a single item: f(x)/2 is the fraction of f(x) and 2.
             */
            bool attachArguments(Item& item)
            {
                if (mPos < mSource.size() && mSource.at(mPos) == '(')
                {
                    Item arguments;

                    if (parsePrimary(arguments))
                    {
                        item.box.append(arguments.box);
                        item.op = QChar();
                        item.group = false;
                    }
                }

                return mError.isEmpty();
            }

            static bool isAsciiLetter(QChar c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }

            bool wordItem(Item& item)
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
                    // a differential (dx, dt, ...) is one item, so that d/dx and dy/dx are fractions of differentials
                    mPos += 2;
                    item.box = textBox(run);
                    return true;
                }

                if (word.isEmpty())
                {
                    ++mPos;
                    return glyphItem(run.at(0), item) && attachArguments(item);
                }

                mPos += word.size();
                const Word& w = words().value(word);

                switch (w.kind)
                {
                case WordKind::Function:
                    item.box = textBox(w.glyphs);
                    item.box.width += 0.04;
                    return attachArguments(item);

                case WordKind::Symbol:
                    return glyphItem(w.glyphs.at(0), item);

                case WordKind::BigOperator:
                    item.bigOperator = true;
                    item.op = w.glyphs.at(0);
                    return true;

                case WordKind::Limit:
                    item.limit = true;
                    return true;

                case WordKind::SquareRoot:
                case WordKind::Vector:
                {
                    Item operand;

                    if (!parsePrimary(operand))
                    {
                        operand.box = placeholderBox();
                    }

                    item.box = w.kind == WordKind::SquareRoot ? sqrtBox(operand.operand()) : vectorBox(operand.operand());
                    return true;
                }
                }

                return false;
            }

            QString mSource;
            int mPos{0};
            QString mError;
    };

    // -------------------------------------------------------------------- handwriting

    /** Small deterministic pseudo random generator (xorshift). */
    class Random
    {
        public:
            explicit Random(quint32 seed) : mState(seed ? seed : 0x9E3779B9u) {}

            /** Uniform in [-1, 1]. */
            qreal next()
            {
                mState ^= mState << 13;
                mState ^= mState >> 17;
                mState ^= mState << 5;
                return (mState / qreal(0xFFFFFFFFu)) * 2.0 - 1.0;
            }

        private:
            quint32 mState;
    };

    /** Split a polyline at sharp corners, so that every piece is smooth. */
    QList<QPolygonF> splitAtCorners(const QPolygonF& stroke, qreal maxTurnDegrees)
    {
        QList<QPolygonF> pieces;
        QPolygonF current;

        for (int i = 0; i < stroke.size(); ++i)
        {
            const QPointF& p = stroke.at(i);

            if (!current.isEmpty() && QLineF(current.last(), p).length() < 1e-6)
            {
                continue;
            }

            current << p;

            if (i > 0 && i + 1 < stroke.size())
            {
                const QLineF in(stroke.at(i - 1), p);
                const QLineF out(p, stroke.at(i + 1));

                if (in.length() > 1e-6 && out.length() > 1e-6)
                {
                    qreal turn = qAbs(in.angleTo(out));
                    turn = qMin(turn, 360.0 - turn);

                    if (turn > maxTurnDegrees)
                    {
                        pieces << current;
                        current = QPolygonF({p});
                    }
                }
            }
        }

        if (current.size() >= 2)
        {
            pieces << current;
        }
        else if (current.size() == 1 && pieces.isEmpty())
        {
            pieces << QPolygonF({current.first(), current.first() + QPointF(0.005, 0)});
        }

        return pieces;
    }

    /** Resample a polyline with points at most `step` apart (the original points are kept). */
    QPolygonF subdivide(const QPolygonF& stroke, qreal step)
    {
        QPolygonF result;

        for (int i = 0; i + 1 < stroke.size(); ++i)
        {
            const QPointF a = stroke.at(i);
            const QPointF b = stroke.at(i + 1);
            const int n = qMax(1, qCeil(QLineF(a, b).length() / step));

            for (int k = 0; k < n; ++k)
            {
                result << a + (b - a) * (qreal(k) / n);
            }
        }

        result << stroke.last();
        return result;
    }

    /** Light Laplacian smoothing, the end points stay in place. */
    QPolygonF smooth(QPolygonF stroke, int passes)
    {
        for (int pass = 0; pass < passes; ++pass)
        {
            QPolygonF next = stroke;

            for (int i = 1; i + 1 < stroke.size(); ++i)
            {
                next[i] = stroke.at(i - 1) * 0.25 + stroke.at(i) * 0.5 + stroke.at(i + 1) * 0.25;
            }

            stroke = next;
        }

        return stroke;
    }
}

// ------------------------------------------------------------------------ UBMathInkLayout

QRectF UBMathInkLayout::Formula::bounds() const
{
    QRectF rect;

    for (const Glyph& glyph : glyphs)
    {
        for (const QPolygonF& stroke : glyph.strokes)
        {
            rect = rect.isNull() ? stroke.boundingRect() : rect.united(stroke.boundingRect());
        }
    }

    for (const QRectF& placeholder : placeholders)
    {
        rect = rect.isNull() ? placeholder : rect.united(placeholder);
    }

    return rect;
}

bool UBMathInkLayout::isAvailable()
{
    return library().loaded;
}

bool UBMathInkLayout::hasGlyph(QChar c)
{
    return library().glyphs.contains(c);
}

UBMathInkLayout::Formula UBMathInkLayout::layout(const QString& source, QString* error)
{
    Formula formula;
    formula.source = source;

    if (!isAvailable())
    {
        if (error)
        {
            *error = QCoreApplication::translate("UBMathInkLayout", "The handwriting glyphs could not be loaded.");
        }

        return formula;
    }

    Parser parser(source);
    QString parseError;
    const Box box = parser.parse(&parseError);

    if (error)
    {
        *error = parseError;
    }

    if (!parseError.isEmpty())
    {
        return formula;
    }

    formula.glyphs = box.glyphs;
    formula.placeholders = box.placeholders;

    if (formula.glyphs.isEmpty() && formula.placeholders.isEmpty() && error && error->isEmpty())
    {
        *error = QCoreApplication::translate("UBMathInkLayout", "Nothing to write.");
    }

    return formula;
}

QList<QPolygonF> UBMathInkLayout::humanize(const Formula& formula, qreal jitter)
{
    QList<QPolygonF> result;
    jitter = qBound(0.0, jitter, 3.0);

    const qreal slant = qMin(0.06 * jitter, 0.15);
    const quint32 formulaSeed = qHash(formula.source) ^ 0x5bd1e995u;
    Random formulaRandom(formulaSeed);
    const qreal wavePhase = formulaRandom.next() * M_PI;

    for (int g = 0; g < formula.glyphs.size(); ++g)
    {
        const Glyph& glyph = formula.glyphs.at(g);
        Random random(formulaSeed ^ (quint32(g + 1) * 2654435761u));

        // per symbol: slightly different angle, size and position, baseline wobble
        const QPointF center = glyph.box.center();
        const qreal angle = jitter * 1.6 * random.next();
        const qreal scale = 1.0 + jitter * 0.03 * random.next();
        const QPointF offset(jitter * 0.008 * random.next(),
                             jitter * (0.01 * random.next() + 0.012 * qSin(center.x() * 2.3 + wavePhase)));

        QTransform transform;
        transform.translate(center.x() + offset.x(), center.y() + offset.y());
        transform.rotate(angle);
        transform.scale(scale, scale);
        transform.translate(-center.x(), -center.y());

        // trembling hand: a smooth displacement field, so that touching strokes stay connected
        const qreal tremble = jitter * 0.0045;
        const qreal f1 = 8.0 + 3.0 * random.next();
        const qreal f2 = 9.0 + 3.0 * random.next();
        const qreal p1 = random.next() * M_PI;
        const qreal p2 = random.next() * M_PI;

        for (const QPolygonF& stroke : glyph.strokes)
        {
            for (const QPolygonF& piece : splitAtCorners(transform.map(stroke), 40.0))
            {
                QPolygonF points = subdivide(piece, 0.012);

                for (QPointF& p : points)
                {
                    p += QPointF(tremble * qSin(p.y() * f1 + p1), tremble * qSin(p.x() * f2 + p2));
                    p.setX(p.x() - slant * p.y());
                }

                result << smooth(points, 2);
            }
        }
    }

    return result;
}
