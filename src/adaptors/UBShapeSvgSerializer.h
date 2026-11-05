/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
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


#ifndef UBSHAPESVGSERIALIZER_H_
#define UBSHAPESVGSERIALIZER_H_

#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

class UBGraphicsShapeItem;

/**
 * @brief Reads and writes UBGraphicsShapeItem as native SVG elements.
 *
 * Shapes are written as <rect>, <ellipse>, <polygon> (triangle), <line> (line without
 * arrow heads) or <path> (line with arrow heads) with standard SVG presentation
 * attributes, so that the page SVG renders in any viewer. Additional ub: attributes
 * carry the information needed by OpenBoard:
 *
 *   ub:type="shape"                        marks the element as shape (vs. strokes)
 *   ub:kind="rectangle|rounded-rectangle|ellipse|triangle|line|arrow"
 *   ub:uuid, ub:z-value, ub:layer, ub:locked, ub:hidden-on-display, ub:background
 *   ub:stroke-on-dark-background, ub:stroke-on-light-background   (#rrggbb / #aarrggbb)
 *   ub:filled="true|false"
 *   ub:fill-on-dark-background, ub:fill-on-light-background       (optional, #aarrggbb)
 *   ub:start-arrow, ub:end-arrow           (lines and arrows)
 *   ub:x1, ub:y1, ub:x2, ub:y2             (end points of lines written as <path>)
 *
 * The geometry is given in item coordinates, the "transform" attribute holds the
 * scene transform of the item (like for all other items).
 */
class UBShapeSvgSerializer
{
    public:
        /// true if the current start element of the reader is a shape element
        static bool isShapeElement(const QXmlStreamReader& reader, const QString& namespaceUri);

        /// create a shape item of the kind given by the current element
        static UBGraphicsShapeItem* createShape(const QXmlStreamReader& reader, const QString& namespaceUri);

        /// read geometry and style of the current element into the shape. Common attributes
        /// (transform, uuid, z-value, ...) are expected to be read before by the caller.
        static void readShape(const QXmlStreamReader& reader, const QString& namespaceUri, UBGraphicsShapeItem* shape);

        /// write the shape as a complete SVG element
        static void writeShape(QXmlStreamWriter& writer, const QString& namespaceUri, const UBGraphicsShapeItem* shape,
                               bool darkBackground, bool isBackground);
};

#endif /* UBSHAPESVGSERIALIZER_H_ */
