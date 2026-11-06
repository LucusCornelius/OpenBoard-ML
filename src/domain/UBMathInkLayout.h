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
#include <QPainterPath>
#include <QRectF>
#include <QString>

/**
 * Typeset maths for the board.
 *
 * Turns a line of "ink text" such as `f(x) = x^2 + 1/(x-1)` into the outlines of a
 * typeset formula, using the bundled OpenType maths font (Latin Modern Math, see
 * UBMathFont) and the layout rules of TeX (The TeXbook, appendix G) as encoded in the
 * OpenType MATH table: display, text, script (70 %) and scriptscript (50 %) styles,
 * script, fraction, radical and limit shifts and gaps, the maths axis, rule thickness,
 * larger operator and delimiter variants and the TeX spacing between atom classes.
 * Variables are italic, digits, function names, d in dx and capital Greek upright.
 * See docs/MATHS.md.
 *
 * Ink text syntax:
 *   x^2  x^(n+1)      superscript (parentheses around a group are dropped)
 *   x_1  a_(n-1)      subscript
 *   f'  f''           primes
 *   a/b  1/(x-1)      stacked fraction (operands: one item or a parenthesised group)
 *   //                an inline slash
 *   sqrt(...)         square root
 *   int sum prod      big operators, bounds with _ and ^ (int_0^1, sum_(n=1)^inf)
 *   iint oint         double and contour integral
 *   lim_(x->a)        limit, the subscript is written below
 *   vec(v)            vector arrow
 *   sin cos tan ln log exp ... function names (upright)
 *   dx dt ...         differentials (upright d)
 *   alpha ... omega, Gamma ... Omega   Greek letters
 *   -> <- => <=> <= >= != ~~ +- *     → ← ⇒ ⇔ ≤ ≥ ≠ ≈ ± ·
 *   inf partial nabla pm times div cdot approx equiv in notin subset
 *   supset cup cap empty forall exists deg perp NN ZZ QQ RR CC
 *   { }               invisible grouping, \{ \} are literal braces
 *   #                 an empty placeholder box (space left for handwriting)
 * Any character of the font (√ ∫ Σ α ≤ ...) can also be typed directly.
 */
class UBMathInkLayout
{
    public:
        /** A laid out formula. Units: em (1.0 = the font size), y axis down, origin at the left end of the baseline. */
        struct Formula
        {
            /** One filled outline per glyph or rule (fraction bar, radical vinculum), non-zero winding fill. */
            QList<QPainterPath> shapes;
            QList<QRectF> placeholders;
            QString source;

            bool isEmpty() const { return shapes.isEmpty(); }

            /** Bounding rectangle of the shapes and the placeholders. */
            QRectF bounds() const;

            /** All shapes in one path. */
            QPainterPath path() const;
        };

        /** True if the maths font could be loaded. */
        static bool isAvailable();

        /**
         * Lay out a line of ink text, in TeX text style (as inline maths, $...$) or display
         * style (full size fractions, limits above and below sums, large operators, $$...$$).
         * Returns an empty formula on error and sets the error message if requested.
         */
        static Formula layout(const QString& source, QString* error = nullptr, bool displayStyle = false);
};

#endif // UBMATHINKLAYOUT_H
