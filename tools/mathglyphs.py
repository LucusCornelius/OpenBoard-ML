#!/usr/bin/env python3
"""
Generate resources/math/inkGlyphs.json: the single-stroke ("plotter style")
glyph library used by the maths ink palette of OpenBoard.

Every glyph is described below with a tiny path language, in design units
(1 em = 100 units, y axis pointing up, baseline at y = 0):

    M x y                   start a new stroke at (x, y)
    L x y                   straight line
    Q cx cy x y             quadratic Bezier
    C c1x c1y c2x c2y x y   cubic Bezier
    O cx cy rx ry a0 a1     new stroke: elliptical arc, angles in degrees (counter-clockwise)
    A cx cy rx ry a0 a1     elliptical arc continuing the current stroke
    D x y                   dot (a very short stroke)

Metrics: x-height 42, capitals and digits 66, ascenders 70, descenders -24,
maths axis (centre of + - =) at 23.

The glyphs were drawn by hand for OpenBoard and are distributed under the
same licence as OpenBoard.

Output format (compact JSON):
    { "metrics": {...}, "glyphs": { "<char>": { "w": advance, "s": [[x0, y0, x1, y1, ...], ...] } } }
with coordinates in em (divided by 100) and the y axis pointing DOWN
(Qt convention), baseline at y = 0.

Usage:  python3 -I tools/mathglyphs.py resources/math/inkGlyphs.json [preview.svg]
"""

import json
import math
import sys

# advance width, path
GLYPHS = {
    # ---------------------------------------------------------------- lowercase
    "a": (44, "O 20 21 14 21 50 410 M 34 42 L 34 6 Q 34 0 40 1"),
    "b": (42, "M 8 70 L 8 2 O 21 21 13 21 180 540"),
    "c": (38, "O 21 21 14 21 40 318"),
    "d": (44, "O 20 21 14 21 30 390 M 34 70 L 34 6 Q 34 0 40 1"),
    "e": (40, "M 8 22 L 34 22 A 21 21 13 21 5 318"),
    "f": (36, "M 36 64 C 33 72 23 73 21 61 L 18 -10 C 16 -22 8 -25 3 -18 M 7 42 L 31 42"),
    "g": (42, "O 20 23 13 19 30 390 M 33 42 L 33 -10 C 33 -26 12 -28 7 -16"),
    "h": (42, "M 8 70 L 8 0 M 8 26 C 13 40 33 46 33 28 L 33 0"),
    "i": (20, "M 10 42 L 10 5 Q 10 0 15 1 D 10 59"),
    "j": (22, "M 15 42 L 15 -12 C 15 -26 4 -27 0 -19 D 15 59"),
    "k": (38, "M 8 70 L 8 0 M 32 43 L 9 18 M 15 24 L 34 0"),
    "l": (20, "M 10 70 L 10 5 Q 10 0 16 1"),
    "m": (52, "M 7 42 L 7 0 M 7 30 C 10 42 25 46 25 30 L 25 0 M 25 30 C 28 42 44 46 44 30 L 44 0"),
    "n": (42, "M 8 42 L 8 0 M 8 28 C 12 40 33 46 33 28 L 33 0"),
    "o": (42, "O 21 21 14 21 100 470"),
    "p": (42, "M 8 42 L 8 -24 O 21 21 13 21 180 540"),
    "q": (42, "O 20 21 14 21 30 390 M 34 42 L 34 -24"),
    "r": (34, "M 8 42 L 8 0 M 8 26 C 12 38 22 44 32 40"),
    "s": (38, "M 32 37 C 28 44 9 44 9 34 C 9 24 31 23 32 12 C 33 -2 9 -2 6 7"),
    "t": (32, "M 16 62 L 16 6 Q 16 0 26 2 M 6 42 L 30 42"),
    "u": (42, "M 8 42 L 8 14 C 8 -4 31 -4 33 14 M 33 42 L 33 5 Q 33 0 39 1"),
    "v": (40, "M 6 42 L 20 0 L 34 42"),
    "w": (54, "M 5 42 L 15 0 L 27 34 L 38 0 L 49 42"),
    "x": (40, "M 7 42 L 33 0 M 33 42 L 7 0"),
    "y": (40, "M 6 42 L 20 4 M 34 42 L 16 -15 Q 11 -25 4 -23"),
    "z": (40, "M 8 42 L 33 42 L 7 0 L 34 0"),
    # ---------------------------------------------------------------- uppercase
    "A": (50, "M 6 0 L 25 66 L 44 0 M 13 22 L 37 22"),
    "B": (46, "M 8 0 L 8 66 L 25 66 C 43 66 43 35 25 35 L 8 35 M 25 35 C 47 35 47 0 25 0 L 8 0"),
    "C": (50, "O 28 33 21 33 45 318"),
    "D": (48, "M 8 0 L 8 66 L 18 66 C 50 66 50 0 18 0 L 8 0"),
    "E": (42, "M 36 66 L 8 66 L 8 0 L 36 0 M 8 34 L 30 34"),
    "F": (40, "M 36 66 L 8 66 L 8 0 M 8 34 L 30 34"),
    "G": (52, "O 28 33 21 33 45 318 L 46 30 L 32 30"),
    "H": (50, "M 8 66 L 8 0 M 42 66 L 42 0 M 8 34 L 42 34"),
    "I": (22, "M 4 66 L 18 66 M 11 66 L 11 0 M 4 0 L 18 0"),
    "J": (40, "M 33 66 L 33 18 C 33 -6 8 -6 6 14"),
    "K": (46, "M 8 66 L 8 0 M 40 66 L 9 26 M 18 37 L 42 0"),
    "L": (40, "M 8 66 L 8 0 L 36 0"),
    "M": (56, "M 7 0 L 9 66 L 28 18 L 47 66 L 49 0"),
    "N": (50, "M 8 0 L 8 66 L 42 0 L 42 66"),
    "O": (56, "O 28 33 21 33 100 470"),
    "P": (44, "M 8 0 L 8 66 L 25 66 C 45 66 45 32 25 32 L 8 32"),
    "Q": (56, "O 28 33 21 33 100 470 M 32 14 L 50 -4"),
    "R": (46, "M 8 0 L 8 66 L 25 66 C 45 66 45 32 25 32 L 8 32 M 24 32 L 42 0"),
    "S": (48, "M 40 56 C 36 68 11 70 10 54 C 9 38 42 37 42 18 C 42 -4 8 -4 6 12"),
    "T": (48, "M 4 66 L 44 66 M 24 66 L 24 0"),
    "U": (50, "M 8 66 L 8 20 C 8 -6 42 -6 42 20 L 42 66"),
    "V": (50, "M 5 66 L 25 0 L 45 66"),
    "W": (60, "M 4 66 L 16 0 L 30 50 L 44 0 L 56 66"),
    "X": (48, "M 6 66 L 42 0 M 42 66 L 6 0"),
    "Y": (48, "M 5 66 L 24 34 L 43 66 M 24 34 L 24 0"),
    "Z": (49, "M 7 66 L 42 66 L 6 0 L 43 0"),
    # ---------------------------------------------------------------- digits
    "0": (44, "O 22 33 16 33 100 465"),
    "1": (44, "M 12 54 L 26 66 L 26 0"),
    "2": (44, "M 8 50 C 10 69 38 70 38 50 C 38 34 16 22 7 0 L 39 0"),
    "3": (44, "M 8 58 C 15 72 40 68 38 50 C 37 39 26 36 18 36 C 31 36 42 30 41 16 C 40 -6 10 -4 6 10"),
    "4": (44, "M 30 0 L 30 66 L 5 20 L 40 20"),
    "5": (44, "M 36 66 L 12 66 L 9 37 C 18 44 40 44 40 20 C 40 -6 10 -4 6 10"),
    "6": (44, "M 34 63 C 22 72 6 60 6 24 C 6 -6 38 -6 38 20 C 38 42 8 42 6 22"),
    "7": (44, "M 6 66 L 38 66 L 16 0"),
    "8": (44, "M 22 35 C 6 39 6 66 22 66 C 38 66 38 39 22 35 C 3 31 4 0 22 0 C 40 0 41 31 22 35"),
    "9": (44, "M 38 46 C 38 25 6 25 6 46 C 6 70 38 70 38 46 L 36 18 C 34 0 18 -2 9 5"),
    # ---------------------------------------------------------------- punctuation, brackets
    "(": (26, "M 21 72 C 4 52 4 -2 21 -22"),
    ")": (26, "M 5 72 C 22 52 22 -2 5 -22"),
    "[": (24, "M 20 72 L 8 72 L 8 -22 L 20 -22"),
    "]": (24, "M 4 72 L 16 72 L 16 -22 L 4 -22"),
    "{": (28, "M 24 72 C 13 72 15 58 15 42 C 15 29 13 25 5 25 C 13 25 15 21 15 8 C 15 -8 13 -22 24 -22"),
    "}": (28, "M 4 72 C 15 72 13 58 13 42 C 13 29 15 25 23 25 C 15 25 13 21 13 8 C 13 -8 15 -22 4 -22"),
    "|": (18, "M 9 72 L 9 -22"),
    ",": (16, "M 9 3 L 6 -11"),
    ".": (16, "D 8 2"),
    ":": (18, "D 9 30 D 9 3"),
    ";": (18, "D 10 30 M 10 3 L 7 -11"),
    "!": (20, "M 10 66 L 10 18 D 10 2"),
    "?": (40, "M 8 54 C 10 70 38 70 38 52 C 38 38 21 38 21 22 L 21 14 D 21 2"),
    "'": (14, "M 11 70 L 6 50"),
    "/": (36, "M 30 70 L 6 -16"),
    "%": (52, "M 42 66 L 10 0 O 14 54 7 11 0 360 O 38 12 7 11 0 360"),
    # ---------------------------------------------------------------- operators and relations
    "+": (46, "M 6 23 L 40 23 M 23 6 L 23 40"),
    "-": (46, "M 6 23 L 40 23"),
    "−": (46, "M 6 23 L 40 23"),             # minus sign
    "=": (46, "M 6 31 L 40 31 M 6 15 L 40 15"),
    "±": (46, "M 6 32 L 40 32 M 23 15 L 23 49 M 6 5 L 40 5"),    # plus-minus
    "×": (46, "M 11 11 L 35 35 M 35 11 L 11 35"),               # times
    "÷": (46, "M 6 23 L 40 23 D 23 38 D 23 8"),                 # division
    "·": (22, "D 11 23"),                                       # dot operator
    "<": (46, "M 38 42 L 8 23 L 38 4"),
    ">": (46, "M 8 42 L 38 23 L 8 4"),
    "≤": (46, "M 38 50 L 8 34 L 38 18 M 8 4 L 38 4"),           # <=
    "≥": (46, "M 8 50 L 38 34 L 8 18 M 8 4 L 38 4"),            # >=
    "≠": (46, "M 6 31 L 40 31 M 6 15 L 40 15 M 31 44 L 15 2"),  # not equal
    "≈": (46, "M 6 29 C 12 38 18 38 23 33 C 28 28 34 28 40 37 M 6 11 C 12 20 18 20 23 15 C 28 10 34 10 40 19"),
    "~": (46, "M 6 20 C 12 29 18 29 23 24 C 28 19 34 19 40 28"),
    "≡": (46, "M 6 36 L 40 36 M 6 23 L 40 23 M 6 10 L 40 10"),  # identical
    "→": (54, "M 6 23 L 46 23 M 36 32 L 47 23 L 36 14"),        # right arrow
    "←": (54, "M 48 23 L 8 23 M 18 32 L 7 23 L 18 14"),         # left arrow
    "⇒": (56, "M 6 31 L 42 31 M 6 15 L 42 15 M 35 41 L 50 23 L 35 5"),          # implies
    "⇔": (60, "M 18 31 L 42 31 M 18 15 L 42 15 M 39 41 L 54 23 L 39 5 M 21 41 L 6 23 L 21 5"),
    "∈": (44, "M 38 42 L 25 42 C 6 42 6 4 25 4 L 38 4 M 10 23 L 36 23"),        # element of
    "∉": (44, "M 38 42 L 25 42 C 6 42 6 4 25 4 L 38 4 M 10 23 L 36 23 M 31 50 L 15 -6"),
    "⊂": (44, "M 38 42 L 25 42 C 6 42 6 4 25 4 L 38 4"),        # subset
    "⊃": (44, "M 6 42 L 19 42 C 38 42 38 4 19 4 L 6 4"),        # superset
    "∪": (46, "M 8 44 L 8 20 C 8 -3 38 -3 38 20 L 38 44"),      # union
    "∩": (46, "M 8 0 L 8 24 C 8 47 38 47 38 24 L 38 0"),        # intersection
    "∅": (46, "O 23 24 15 19 90 455 M 37 48 L 9 -2"),           # empty set
    "∀": (48, "M 6 66 L 24 0 L 42 66 M 12 44 L 36 44"),         # for all
    "∃": (42, "M 8 66 L 34 66 L 34 0 L 8 0 M 12 33 L 34 33"),   # exists
    "°": (22, "O 11 59 6 7 90 455"),                            # degree
    "∞": (56, "M 28 23 C 20 38 6 36 6 23 C 6 10 20 8 28 23 C 36 38 50 36 50 23 C 50 10 36 8 28 23"),
    "∂": (44, "M 10 58 C 18 71 38 69 38 42 L 38 26 C 38 6 28 0 20 0 C 8 0 6 10 6 16 C 6 30 24 36 38 25"),
    "∇": (50, "M 6 66 L 44 66 L 25 0 L 6 66"),                  # nabla
    "⊥": (46, "M 23 46 L 23 0 M 4 0 L 42 0"),                   # perpendicular
    "∥": (32, "M 11 52 L 11 -6 M 21 52 L 21 -6"),               # parallel
    "∠": (48, "M 40 46 L 6 0 L 44 0"),                          # angle
    # big operators (laid out larger by the formula engine)
    "∫": (34, "M 33 70 C 31 79 21 78 20 66 L 14 -14 C 13 -27 3 -27 1 -18"),                 # integral
    "∬": (46, "M 33 70 C 31 79 21 78 20 66 L 14 -14 C 13 -27 3 -27 1 -18 M 45 70 C 43 79 33 78 32 66 L 26 -14 C 25 -27 15 -27 13 -18"),
    "∮": (34, "M 33 70 C 31 79 21 78 20 66 L 14 -14 C 13 -27 3 -27 1 -18 O 17 26 9 10 90 455"),
    "∑": (50, "M 42 66 L 6 66 L 27 33 L 6 0 L 44 0"),           # n-ary sum
    "∏": (52, "M 4 66 L 48 66 M 13 66 L 13 0 M 39 66 L 39 0"),  # n-ary product
    "√": (40, "M 2 30 L 9 35 L 19 0 L 38 76"),                  # radical (without vinculum)
    # ---------------------------------------------------------------- double-struck sets
    "ℕ": (50, "M 8 0 L 8 66 L 42 0 L 42 66 M 14 56 L 14 0"),
    "ℤ": (50, "M 7 66 L 44 66 L 14 0 M 36 66 L 6 0 L 44 0"),
    "ℚ": (56, "O 28 33 21 33 100 470 M 15 52 L 15 14 M 32 14 L 50 -4"),
    "ℝ": (46, "M 8 0 L 8 66 L 25 66 C 45 66 45 32 25 32 L 8 32 M 24 32 L 42 0 M 14 66 L 14 0"),
    "ℂ": (50, "O 28 33 21 33 45 318 M 14 56 L 14 10"),
    # ---------------------------------------------------------------- Greek lowercase
    "α": (46, "M 40 42 C 34 18 28 0 17 0 C 6 0 4 15 6 24 C 8 36 16 42 22 42 C 32 42 34 18 42 0"),
    "β": (44, "M 10 -24 L 10 52 C 10 72 37 72 36 55 C 35 41 23 40 18 40 C 34 40 42 32 40 18 C 38 0 16 -4 10 8"),
    "γ": (42, "M 4 34 C 10 44 16 42 20 24 L 22 -18 C 22 -26 15 -26 16 -17 C 19 1 30 26 38 42"),
    "δ": (44, "M 33 64 C 27 72 10 70 12 60 C 14 52 38 40 38 20 C 38 4 30 0 22 0 C 10 0 6 10 6 20 C 6 34 18 42 28 46"),
    "ε": (40, "M 34 38 C 26 46 8 44 10 32 C 12 24 20 22 26 22 C 16 22 6 18 6 10 C 6 -2 26 -2 34 6"),
    "ζ": (40, "M 10 70 C 14 64 22 64 32 70 C 12 50 6 34 6 18 C 6 4 30 4 30 -8 C 30 -18 24 -22 18 -22"),
    "η": (42, "M 6 36 C 8 42 12 42 12 34 L 12 0 M 12 28 C 16 40 34 46 34 28 L 34 -24"),
    "θ": (42, "O 21 33 14 33 90 455 M 7 33 L 35 33"),
    "κ": (40, "M 8 42 L 8 0 M 32 43 L 9 18 M 15 24 L 34 0"),
    "λ": (44, "M 6 66 C 12 71 18 66 20 56 L 38 0 M 22 46 L 6 0"),
    "μ": (42, "M 8 42 L 8 -24 M 8 14 C 8 -4 31 -4 33 14 M 33 42 L 33 5 Q 33 0 39 1"),
    "ν": (40, "M 6 42 L 18 0 C 30 14 36 30 34 42"),
    "ξ": (40, "M 32 70 C 18 70 12 66 12 58 C 12 50 22 46 30 46 C 14 46 6 40 6 30 C 6 18 18 14 26 14 C 34 14 34 0 26 -8 C 22 -14 18 -18 16 -22"),
    "π": (46, "M 4 36 C 10 44 16 42 24 42 L 42 42 M 14 42 C 14 24 12 8 8 0 M 32 42 L 32 6 Q 33 0 40 2"),
    "ρ": (42, "M 8 -24 L 8 20 C 8 46 36 46 36 22 C 36 -2 12 -2 8 10"),
    "σ": (46, "M 42 42 L 22 42 C 6 42 4 26 6 18 C 8 4 16 0 22 0 C 34 0 38 14 36 24 C 34 36 28 42 22 42"),
    "τ": (42, "M 4 36 C 10 44 16 42 24 42 L 40 42 M 22 42 L 20 7 Q 20 0 28 2"),
    "φ": (48, "O 24 21 17 21 90 455 M 24 66 L 24 -24"),
    "χ": (44, "M 4 40 C 10 44 14 38 20 22 L 28 -12 C 32 -22 36 -24 40 -20 M 38 40 L 6 -22"),
    "ψ": (48, "M 6 40 L 6 24 C 6 0 42 0 42 24 L 42 42 M 24 66 L 24 -24"),
    "ω": (50, "M 12 42 C 2 30 4 0 14 0 C 22 0 25 10 25 20 C 25 10 28 0 36 0 C 46 0 48 30 38 42"),
    # ---------------------------------------------------------------- Greek uppercase
    "Γ": (40, "M 36 66 L 8 66 L 8 0"),
    "Δ": (52, "M 4 0 L 26 66 L 48 0 L 4 0"),
    "Θ": (56, "O 28 33 21 33 100 465 M 15 33 L 41 33"),
    "Λ": (50, "M 4 0 L 25 66 L 46 0"),
    "Ξ": (46, "M 6 66 L 40 66 M 12 33 L 34 33 M 6 0 L 40 0"),
    "Π": (52, "M 4 66 L 48 66 M 13 66 L 13 0 M 39 66 L 39 0"),
    "Σ": (50, "M 42 66 L 6 66 L 27 33 L 6 0 L 44 0"),
    "Φ": (56, "O 28 33 21 17 90 455 M 28 66 L 28 0"),
    "Ψ": (52, "M 6 52 L 6 42 C 6 18 46 18 46 42 L 46 52 M 26 66 L 26 0"),
    "Ω": (56, "M 4 0 L 18 0 C 2 14 4 66 28 66 C 52 66 54 14 38 0 L 52 0"),
}


def bezier_points(p0, p1, p2, p3, step=2.0):
    length = math.dist(p0, p1) + math.dist(p1, p2) + math.dist(p2, p3)
    n = max(4, int(length / step))
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        x = u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0]
        y = u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]
        out.append((x, y))
    return out


def arc_points(cx, cy, rx, ry, a0, a1):
    span = a1 - a0
    n = max(4, int(abs(span) / 7))
    return [(cx + rx * math.cos(math.radians(a0 + span * i / n)),
             cy + ry * math.sin(math.radians(a0 + span * i / n))) for i in range(n + 1)]


def parse(path):
    tokens = path.split()
    strokes = []
    current = None
    i = 0

    def nums(count):
        nonlocal i
        values = [float(t) for t in tokens[i + 1:i + 1 + count]]
        if len(values) != count:
            raise ValueError("bad path: " + path)
        i += count + 1
        return values

    while i < len(tokens):
        cmd = tokens[i]
        if cmd == "M":
            x, y = nums(2)
            current = [(x, y)]
            strokes.append(current)
        elif cmd == "L":
            x, y = nums(2)
            current.append((x, y))
        elif cmd == "Q":
            cx, cy, x, y = nums(4)
            p0 = current[-1]
            c1 = (p0[0] + 2 / 3 * (cx - p0[0]), p0[1] + 2 / 3 * (cy - p0[1]))
            c2 = (x + 2 / 3 * (cx - x), y + 2 / 3 * (cy - y))
            current.extend(bezier_points(p0, c1, c2, (x, y)))
        elif cmd == "C":
            x1, y1, x2, y2, x, y = nums(6)
            current.extend(bezier_points(current[-1], (x1, y1), (x2, y2), (x, y)))
        elif cmd in ("O", "A"):
            pts = arc_points(*nums(6))
            if cmd == "O" or current is None:
                current = list(pts)
                strokes.append(current)
            else:
                current.extend(pts)
        elif cmd == "D":
            x, y = nums(2)
            current = [(x - 0.8, y - 0.4), (x + 0.8, y + 0.4)]
            strokes.append(current)
        else:
            raise ValueError("unknown command %r in %s" % (cmd, path))
    return strokes


def build():
    glyphs = {}
    for name, (advance, path) in GLYPHS.items():
        strokes = []
        for stroke in parse(path):
            flat = []
            for x, y in stroke:
                flat.extend((round(x / 100.0, 4), round(-y / 100.0, 4)))
            strokes.append(flat)
        glyphs[name] = {"w": advance / 100.0, "s": strokes}
    return {
        "format": 1,
        "comment": "Single-stroke glyphs for the OpenBoard maths ink palette, generated by tools/mathglyphs.py. "
                   "Units: em, y axis down, baseline at 0.",
        "metrics": {"xHeight": 0.42, "capHeight": 0.66, "ascender": 0.70, "descender": 0.24, "axis": 0.23},
        "glyphs": glyphs,
    }


def preview(data, filename):
    names = list(data["glyphs"].keys())
    cols = 16
    cell = 70
    rows = (len(names) + cols - 1) // cols
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (cols * cell, rows * cell),
           '<rect width="100%" height="100%" fill="white"/>']
    for k, name in enumerate(names):
        ox = (k % cols) * cell + 8
        oy = (k // cols) * cell + 48
        g = data["glyphs"][name]
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#cce" stroke-width="0.5"/>' % (ox, oy, ox + g["w"] * 50, oy))
        for s in g["s"]:
            pts = " ".join("%.1f,%.1f" % (ox + s[i] * 50, oy + s[i + 1] * 50) for i in range(0, len(s), 2))
            out.append('<polyline points="%s" fill="none" stroke="#1040a0" stroke-width="2" '
                       'stroke-linecap="round" stroke-linejoin="round"/>' % pts)
    out.append("</svg>")
    with open(filename, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = build()
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
        f.write("\n")
    if len(sys.argv) > 2:
        preview(data, sys.argv[2])
    print("%d glyphs written to %s" % (len(data["glyphs"]), sys.argv[1]))
