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

#ifndef UBMATHINKLAYOUT_H
#define UBMATHINKLAYOUT_H

#include <QList>
#include <QPolygonF>
#include <QRectF>
#include <QString>

/**
 * Maths notation as handwriting.
 *
 * A small formula layout engine that turns a line of "ink text" such as
 * `f(x) = x^2 + 1/(x-1)` into single-line strokes (polylines without width),
 * using the hand-drawn single-stroke glyph library in `:/math/inkGlyphs.json`.
 * The strokes are later drawn on the board as ordinary pen strokes.
 *
 * Ink text syntax:
 *   x^2  x^(n+1)      superscript (parentheses around a group are dropped)
 *   x_1  a_(n-1)      subscript
 *   a/b  1/(x-1)      stacked fraction (operands: one item or a parenthesised group)
 *   //               an inline slash
 *   sqrt(...)         square root with vinculum
 *   int sum prod      big operators, bounds with _ and ^ (int_0^1, sum_(n=1)^inf)
 *   iint oint         double and contour integral
 *   lim_(x->a)        limit, the subscript is written below
 *   vec(v)            vector arrow
 *   sin cos tan ln log exp ... function names
 *   alpha ... omega, Gamma ... Omega   Greek letters
 *   -> <- => <=> <= >= != ~~ +- *     → ← ⇒ ⇔ ≤ ≥ ≠ ≈ ± ·
 *   inf partial nabla pm times div cdot approx equiv in notin subset
 *   supset cup cap empty forall exists deg perp NN ZZ QQ RR CC
 *   { }               invisible grouping, \{ \} are literal braces
 *   #                 an empty placeholder box (space left for handwriting)
 * Any character of the glyph library (√ ∫ Σ α ≤ ...) can also be typed directly.
 */
class UBMathInkLayout
{
    public:
        /** One hand-drawn symbol: its strokes, in em units (y axis down, baseline at y = 0). */
        struct Glyph
        {
            QList<QPolygonF> strokes;
            QRectF box;
        };

        /** A laid out formula. The origin is the left end of the baseline. */
        struct Formula
        {
            QList<Glyph> glyphs;
            QList<QRectF> placeholders;
            QString source;

            bool isEmpty() const { return glyphs.isEmpty(); }

            /** Bounding rectangle of the strokes and the placeholders. */
            QRectF bounds() const;
        };

        /** True if the glyph library could be loaded. */
        static bool isAvailable();

        /** True if the glyph library contains the given character. */
        static bool hasGlyph(QChar c);

        /**
         * Lay out a line of ink text. Returns an empty formula on error and sets
         * the error message if requested.
         */
        static Formula layout(const QString& source, QString* error = nullptr);

        /**
         * Smooth the coarse glyph polylines and add a subtle, deterministic handwriting
         * imperfection (slant, baseline wobble, slightly varying size and angle, trembling).
         * @param jitter strength, 0 is "plotter exact", 1 is the default.
         * Sharp corners split the strokes, so that they get round pen joins.
         * Returns polylines in em units.
         */
        static QList<QPolygonF> humanize(const Formula& formula, qreal jitter);
};

#endif // UBMATHINKLAYOUT_H
