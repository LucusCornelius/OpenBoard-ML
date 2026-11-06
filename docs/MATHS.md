# Maths notation: typesetting standards and implementation

The Maths palette writes formulas on the board the way LaTeX typesets them: the glyphs of
a real OpenType maths font (Latin Modern Math, the Computer Modern look of TeX), laid out
with the rules of TeX as they are encoded in the OpenType `MATH` table. This document
collects the standards the implementation follows, with sources, and describes what was
implemented.

## 1. Research

### 1.1 TeX's layout rules (The TeXbook, appendix G)

Knuth's appendix G describes, rule by rule, how TeX turns a math list into boxes. The
parameters come from the maths fonts: `\fontdimen` 8 to 22 of family 2 (σ, `cmsy10`) and
8 to 13 of family 3 (ξ, `cmex10`). Illustrated explanations: Jackowski, *Appendix G
illuminated*, TUGboat 27 (2006) <https://tug.org/TUGboat/tb27-1/tb86jackowski.pdf>, and
Vieth, *OpenType math illuminated*, TUGboat 30 (2009)
<https://www.tug.org/tugboat/tb30-1/tb94vieth.pdf>. The mapping from the TeX parameters to
the OpenType constants: <https://github.com/phst/unimath-extras/blob/master/font-parameters.rst>.

* **Styles.** Display (D), text (T), script (S), scriptscript (SS), each also "cramped"
  (exponents raised less: denominators, subscripts, under a radical). A formula on its own
  line is in display style (`$$...$$`, `\[...\]`), maths in a line of text in text style
  (`$...$`). Superscripts and subscripts of D and T are in S, scripts of S and SS in SS. The
  numerator and denominator of a fraction in D are in T, in T in S, in S in SS.
* **Sizes.** Plain TeX and LaTeX use 10 pt for D and T, 7 pt for S and 5 pt for SS
  (`\textfont`, `\scriptfont`, `\scriptscriptfont`; LaTeX `\DeclareMathSizes{10}{10}{7}{5}`,
  <https://www.latex-project.org/help/documentation/fntguide.pdf>), i.e. **70 %** and
  **50 %**. The fonts for 7 and 5 pt are designed for that size (wider, heavier), the
  OpenType equivalent is the `ssty` feature.
* **Superscripts and subscripts (rule 18).** For a character base: superscript raised by
  sup1 (D, 0.413 em), sup2 (T, S; 0.363 em) or sup3 (cramped, 0.289 em), at least so that its
  bottom is ¼ x-height above the baseline; subscript lowered by sub1 (0.15 em) or, with a
  superscript, sub2 (0.247 em), its top at most ⅘ x-height high. For any other base
  (a group, a big operator) the scripts are placed relative to the top and bottom of the base
  (sup_drop, sub_drop). The gap between superscript and subscript is at least 4 θ; the
  superscript sits after the italic correction of the base, the subscript does not;
  `\scriptspace` (0.5 pt) follows the scripts.
* **Fractions (rule 15).** Numerator shifted up by num1 (D, 0.677 em) or num2 (T, 0.394 em),
  denominator down by denom1 (0.686 em) or denom2 (0.345 em); the rule of thickness θ lies on
  the **maths axis** (σ22 = 0.25 em above the baseline, the height of the middle of the minus
  sign); clearance between rule and numerator/denominator at least 3 θ (D) or θ (T).
  `\nulldelimiterspace` (1.2 pt) on both sides.
* **Default rule thickness** θ = ξ8 = **0.4 pt at 10 pt** (0.04 em): fraction bars, radical
  vinculum, overlines.
* **Radicals (rule 11).** Clearance φ = θ + ¼ |σ5| in D (0.148 em) and 1¼ θ otherwise;
  the radical sign is the smallest variant at least as high as the content plus clearance
  plus θ; if it is taller, half of the excess is added to the clearance; the vinculum
  (thickness θ) continues the top of the sign.
* **Big operators (rule 13).** In display style the larger variant from `cmex10` (∑ 1.4 em,
  ∫ 2.2 em high), centred on the axis. ∑ and ∏ take their limits above and below in display
  style (`\displaylimits`), the integral at the side (LaTeX `\nolimits`). Limit spacing:
  ξ9 to ξ13 (big_op_spacing1-5).
* **Delimiters (rule 19).** `\left(` ... `\right)` grow to cover the content, size at least
  `\delimiterfactor` 901/1000 of the content height around the axis and at most
  `\delimitershortfall` 5 pt less, centred on the axis.
* **Spacing between atoms (TeXbook p. 170).** Each item is an atom of class Ord, Op, Bin,
  Rel, Open, Close, Punct or Inner. Thin space `\thinmuskip` = 3 mu, medium `\medmuskip` =
  4 mu, thick `\thickmuskip` = 5 mu, 1 mu = 1/18 em: Ord–Op thin, Ord–Bin medium, Ord–Rel
  thick, Punct–Ord thin, ...; medium and thick spaces (and some thin ones) vanish in
  script styles. A Bin without a left operand becomes Ord (unary minus).
  <https://www.overleaf.com/learn/latex/Spacing_in_math_mode>,
  <https://ctan.math.washington.edu/tex-archive/macros/luatex/latex/innerscript/innerscript-user-guide.pdf>.

### 1.2 The OpenType MATH table

Microsoft's specification <https://learn.microsoft.com/en-us/typography/opentype/spec/math>
encodes the same parameters per font: `MathConstants` (ScriptPercentScaleDown,
ScriptScriptPercentScaleDown, AxisHeight, SuperscriptShiftUp(Cramped), SubscriptShiftDown,
SubscriptTopMax, SuperscriptBottomMin, SubSuperscriptGapMin, SuperscriptBottomMaxWithSubscript,
SpaceAfterScript, Upper/LowerLimitGapMin and BaselineRise/DropMin,
FractionNumerator(DisplayStyle)ShiftUp, FractionDenominator(DisplayStyle)ShiftDown,
FractionNum/DenomGapMin, FractionRuleThickness, RadicalVerticalGap,
RadicalDisplayStyleVerticalGap, RadicalRuleThickness, RadicalExtraAscender,
DisplayOperatorMinHeight, ...), `MathGlyphInfo` (italic corrections, top accent attachment)
and `MathVariants` (larger glyph variants and glyph assemblies for delimiters, radicals,
integrals, arrows). The W3C MathML Core specification describes the layout algorithms on
top of these constants: <https://w3c.github.io/mathml-core/> (fractions §3.3.2, radicals
§3.3.3, scripts §3.4.1, large operators §3.2.4.3).

Values (em) read from the fonts themselves, next to TeX's `cmsy10`/`cmex10`:

| Parameter | TeX (CM 10 pt) | Latin Modern Math | STIX Two Math |
|---|---|---|---|
| Script / scriptscript scale | 70 % / 50 % | 70 % / 50 % | 70 % / 55 % |
| Axis height | 0.25 | 0.25 | 0.258 |
| Default rule (fraction bar, radical) | 0.04 | 0.04 | 0.068 |
| Superscript shift up (T) | 0.363 (sup2) | 0.363 | 0.36 |
| Subscript shift down | 0.15 / 0.247 | 0.247 | 0.21 |
| Numerator shift up D / T | 0.677 / 0.394 | 0.677 / 0.394 | 0.64 / 0.585 |
| Denominator shift down D / T | 0.686 / 0.345 | 0.686 / 0.345 | 0.64 / 0.585 |
| Radical gap D / T | 0.148 / 0.05 | 0.148 / 0.05 | 0.17 / 0.085 |
| Display operator min. height | (∑ 1.4) | 1.3 | 1.8 |
| Space after script | 0.05 | 0.056 | 0.04 |
| x-height / cap height | 0.431 / 0.683 | 0.431 / 0.683 | 0.473 / 0.657 |

Latin Modern Math reproduces TeX's Computer Modern parameters exactly.

### 1.3 Typeface conventions

ISO 80000-2:2019 (preview <https://cdn.standards.iteh.ai/samples/64973/329519100abd447ea0d49747258d1094/ISO-80000-2-2019.pdf>),
the ISO house style <https://www.iso.org/ISO-house-style.html> and summaries such as
<https://nhigham.com/2016/01/28/typesetting-mathematics-according-to-the-iso-standard/>:

* **italic**: variables, running indices, parameters, general functions (x, n, a, f, g);
  lower case Greek variables (α, π as a variable);
* **upright**: digits; functions with a fixed meaning (sin, cos, tan, ln, log, exp, lim,
  max, min, det); the differential operator d (dx, d/dx); mathematical constants e, i, π
  in ISO (TeX sets e, i, π italic by default); units; capital Greek in TeX (Γ, Δ, Σ, Ω);
* **bold** (italic) for vectors and matrices is an option; the arrow accent (v⃗) is the
  school convention.

Conventional maths fonts and their licences:

| Font | Look | Licence | Bundling |
|---|---|---|---|
| Latin Modern Math | Computer Modern (TeX default) | GUST Font License = LPPL 1.3c, <https://www.gust.org.pl/fonts/licenses/GUST-FONT-LICENSE.txt> | yes, unmodified, with licence and a notice where to get the complete work |
| New Computer Modern Math | Computer Modern | GUST Font License | yes (as above) |
| STIX Two Math | Times-like | SIL OFL 1.1, <https://github.com/stipub/stixfonts> | yes |
| TeX Gyre Pagella / Termes / Schola Math | Palatino / Times / Century | GUST Font License | yes |
| Libertinus Math | Linux Libertine | SIL OFL 1.1 | yes |
| Fira Math | Fira Sans (sans serif) | SIL OFL 1.1 | yes |
| Cambria Math | Microsoft Office | proprietary | no |

LPPL 1.3c (<https://www.latex-project.org/lppl/lppl-1-3c/>): distributing only a part of a
work counts as a modification; clause 6 allows it with a notice of the change (here: only
the unmodified font file is included), no implied support, and information on how to get
the complete work.

### 1.4 Sizes

* In print, maths is set at the size of the surrounding text (10 to 12 pt); only scripts are
  smaller (7 and 5 pt at 10 pt). On slides the body text is usually 24 to 32 pt.
* Legibility from a distance (signage rule of thumb): about 1 inch (2.5 cm) of capital height
  per 10 ft (3 m) as the maximum readable distance, comfortable at roughly half of that
  (<https://www.panniergraphics.com/blog/letter-height-visibility-chart-for-outdoor-sign-readability>,
  <https://printerflo.com/tools/letter-height-calculator/>).
* On the board one centimetre is the page grid unit (calibrated to real centimetres on the
  control screen, see BACKGROUNDS.md). Handwriting on 1 cm ruled lines has an x-height of
  about 4 to 5 mm and capitals of about 7 mm.
* Computer Modern: x-height 0.431 em, capital and digit height 0.683 em, parentheses 1 em
  (0.75 above, 0.25 below the baseline).

Conclusion: a font size of **28 pt** (1 em = 0.99 cm) gives an x-height of 4.3 mm,
capitals of 6.8 mm and parentheses of one ruled line: the size of the handwriting around it.
The previous default (capitals 2 cm, 1 em = 3 cm, about 85 pt) was three times too large.
The slider offers the usual type sizes 12 to 96 pt; 48 to 72 pt are for a large touch
display read from the back of the room.

The user's reference (LaTeX formula sheets on the board) is set in text style (inline
maths): fractions with script size numerator and denominator, about 0.5 grid cell x-height.

## 2. Implementation

### 2.1 Decision: a real OpenType maths font, written as filled ink

* **Font**: Latin Modern Math 1.959 (`resources/math/latinmodern-math.otf`, unmodified, with
  `GUST-FONT-LICENSE.txt` and `README-Latin-Modern-Math.txt`), embedded as the Qt resource
  `:/math/latinmodern-math.otf`. It *is* the Computer Modern look of the reference, has the
  complete MATH table and script alternates, and may be redistributed. No new native
  dependency: Qt reads it with `QRawFont` (glyph outlines, advances) and `QRawFont::fontTable`
  (MATH and GSUB tables). It is not registered with `QFontDatabase`, so it does not appear in
  the font list of text boxes.
* **Item type**: the laid out formula is written as **filled ink**: one
  `UBGraphicsPolygonItem` per glyph (or rule), all in one `UBGraphicsStroke` and one
  `UBGraphicsStrokesGroup`, one undo step (`UBGraphicsScene::addInkShapes`). The contours of
  a glyph are joined into one polygon with zero width bridges (`QPainterPath::toFillPolygon`)
  and filled with the non-zero winding rule, so counters (o, 0, 8, ∂) stay open. Curves are
  flattened at 4x the scene resolution so that zooming in stays smooth.
  Compared with an SVG item this keeps everything ink has: the pen colours for light *and*
  dark backgrounds (the formula turns white on a dark page), the stroke eraser (it keeps the
  holes, as it subtracts with the winding rule), selection, moving, copy, z-order, undo,
  saving as standard SVG `<polygon fill-rule>` in the page (no separate file) and PDF export.
  An SVG item has one fixed colour and needs its own file.

### 2.2 Layout (src/domain/UBMathInkLayout.cpp, src/domain/UBMathFont.cpp)

* The ink text is parsed into a tree (glyph, function name, differential, row, fenced
  group, fraction, scripts, big operator, limit, radical, vector, placeholder) and laid out
  recursively in TeX styles with the constants of the font:
  * styles D/T/S/SS with cramping; scripts at ScriptPercentScaleDown (70 %) and
    ScriptScriptPercentScaleDown (50 %), with the `ssty` script glyphs (like cmmi7/cmmi5);
  * scripts: TeX rule 18 with SuperscriptShiftUp/Cramped, SubscriptShiftDown,
    SuperscriptBottomMin, SubscriptTopMax, baseline drops for non-character bases, the
    sub/superscript gap rule, italic correction (superscript after it, subscript not, big
    operators shift the subscript left by it), SpaceAfterScript;
  * fractions: rule 15 with the display/text shifts and gaps, FractionRuleThickness on the
    AxisHeight, `\nulldelimiterspace` 0.12 em on both sides; a simple fraction inside a
    script is written with a solidus (a^(1/n) gives a¹ᐟⁿ, as in the reference);
  * radicals: rule 11, the smallest √ variant (or the glyph assembly) that covers the
    content, excess split, vinculum of RadicalRuleThickness joined to the sign,
    RadicalExtraAscender;
  * big operators: in display style the variant of at least DisplayOperatorMinHeight,
    centred on the axis; ∑ ∏ lim max min with limits above/below in display style (Upper/
    LowerLimitGapMin, BaselineRise/DropMin, shifted by half the italic correction), as
    scripts in text style; integrals always with side scripts;
  * parentheses and brackets grow with their content (rule 19, delimiter factor 901,
    shortfall 0.5 em), from the font's variants or glyph assemblies;
  * vector arrow: the U+20D7 accent at the top accent attachment of the base, wider bases
    get the horizontal variant or assembly of the arrow;
  * spacing: the TeX atom classes and the inter-atom table, Bin to Ord rules, thin space
    after Op (sin x), before the d of a differential (∫ f(x) dx).
* Fonts styles: Latin letters and lower case Greek are the Unicode mathematical italic
  letters (𝑥, 𝛼); digits, function names (sin, cos, tan, ln, log, exp, lim, ...), the d of
  dx/d/dx and capital Greek are upright. e and π stay italic as in TeX (the reference);
  ε and φ are the curly forms usual in school (TeX `\varepsilon`, `\varphi`).
* Spaces are ignored as in TeX; two or more typed spaces give a `\quad` (1 em) to set
  several formulas side by side.
* Style: **text style** by default (as the reference and inline LaTeX, compact, line height
  close to one ruled line); the palette's *Display* button switches to display style
  (full size fractions, limits above and below, large operators).

### 2.3 Settings

* `Board/MathFontSize`: font size in points, default 28, 12 to 96; 1 pt = 1/72 inch on the
  centimetre of the page grid.
* `Board/MathDisplayStyle`: display style instead of text style (default false).
* Removed: `Board/MathInkSizeCm` (capital height of the old handwriting glyphs) and
  `Board/MathInkJitter`; they are deleted from the user settings on start, so everybody
  starts with the new default size.

### 2.4 Files

* `src/domain/UBMathFont.{h,cpp}`: loads the font, reads MathConstants, italic corrections,
  top accent attachments, MathVariants (variants and assemblies) and the `ssty` alternates.
* `src/domain/UBMathInkLayout.{h,cpp}`: parser and TeX/OpenType layout, result in em.
* `src/gui/UBInkWriter.{h,cpp}`: size, placement preview (filled grey outlines), writing.
* `src/domain/UBGraphicsScene.{h,cpp}`: `addInkShapes` (filled ink as one strokes group).
* `src/gui/UBMathPalette.{h,cpp}`: icons rendered with the font, size slider in pt,
  *Display* toggle.
* `resources/math/`: the font with its licence and README.
