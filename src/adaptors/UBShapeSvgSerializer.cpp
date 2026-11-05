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




#include "UBShapeSvgSerializer.h"

#include <QColor>
#include <QPolygonF>
#include <QRegularExpression>

#include "core/UB.h"
#include "domain/UBGraphicsShapeItem.h"
#include "frameworks/UBStringUtils.h"

#include "core/memcheck.h"

namespace
{
    const QString trueString("true");
    const QString falseString("false");

    QString number(qreal value)
    {
        return QString::number(value, 'g', 10);
    }

    QString colorToString(const QColor& color)
    {
        return color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
    }

    QColor colorFromString(const QString& name)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 4, 0))
        return QColor::fromString(name);
#else
        QColor color;
        color.setNamedColor(name);
        return color;
#endif
    }

    QString transformToString(const QTransform& matrix)
    {
        return QString("matrix(%1, %2, %3, %4, %5, %6)")
                .arg(matrix.m11(), 0, 'g')
                .arg(matrix.m12(), 0, 'g')
                .arg(matrix.m21(), 0, 'g')
                .arg(matrix.m22(), 0, 'g')
                .arg(matrix.dx(), 0, 'g')
                .arg(matrix.dy(), 0, 'g');
    }

    qreal attributeValue(const QXmlStreamAttributes& attributes, const QString& name, qreal defaultValue = 0)
    {
        bool ok = false;
        const qreal value = attributes.value(name).toString().toDouble(&ok);
        return ok ? value : defaultValue;
    }

    qreal ubAttributeValue(const QXmlStreamAttributes& attributes, const QString& ns, const QString& name, qreal defaultValue = 0)
    {
        bool ok = false;
        const qreal value = attributes.value(ns, name).toString().toDouble(&ok);
        return ok ? value : defaultValue;
    }

    QString pointsToString(const QPolygonF& polygon)
    {
        QStringList points;

        for (const QPointF& point : polygon)
        {
            points << number(point.x()) + "," + number(point.y());
        }

        return points.join(" ");
    }

    QPolygonF pointsFromString(const QString& string)
    {
        QPolygonF polygon;
        const QStringList values = string.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);

        for (int i = 0; i + 1 < values.size(); i += 2)
        {
            polygon << QPointF(values.at(i).toDouble(), values.at(i + 1).toDouble());
        }

        return polygon;
    }

    QString polygonPath(const QPolygonF& polygon)
    {
        QString path;

        for (int i = 0; i < polygon.size(); ++i)
        {
            path += QString(i == 0 ? "M %1 %2" : " L %1 %2").arg(number(polygon.at(i).x()), number(polygon.at(i).y()));
        }

        return path + " Z";
    }
}

bool UBShapeSvgSerializer::isShapeElement(const QXmlStreamReader& reader, const QString& namespaceUri)
{
    return reader.attributes().value(namespaceUri, "type").toString() == QLatin1String("shape");
}

UBGraphicsShapeItem* UBShapeSvgSerializer::createShape(const QXmlStreamReader& reader, const QString& namespaceUri)
{
    bool ok = false;
    UBGraphicsShapeItem::Kind kind = UBGraphicsShapeItem::kindFromString(reader.attributes().value(namespaceUri, "kind").toString(), &ok);

    if (!ok)
    {
        // guess the kind from the element
        const QString name = reader.name().toString();

        if (name == "ellipse")
            kind = UBGraphicsShapeItem::Ellipse;
        else if (name == "polygon")
            kind = UBGraphicsShapeItem::Triangle;
        else if (name == "line")
            kind = UBGraphicsShapeItem::Line;
        else if (name == "path")
            kind = UBGraphicsShapeItem::Arrow;
        else
            kind = UBGraphicsShapeItem::Rectangle;
    }

    return new UBGraphicsShapeItem(kind);
}

void UBShapeSvgSerializer::readShape(const QXmlStreamReader& reader, const QString& namespaceUri, UBGraphicsShapeItem* shape)
{
    const QXmlStreamAttributes attributes = reader.attributes();
    const QString name = reader.name().toString();
    const QString& ns = namespaceUri;

    // style
    const qreal strokeWidth = attributeValue(attributes, "stroke-width", shape->strokeWidth());
    shape->setStrokeWidth(strokeWidth);

    const QColor stroke = colorFromString(attributes.value("stroke").toString());
    QColor strokeOnDark = colorFromString(attributes.value(ns, "stroke-on-dark-background").toString());
    QColor strokeOnLight = colorFromString(attributes.value(ns, "stroke-on-light-background").toString());

    if (!strokeOnDark.isValid())
        strokeOnDark = stroke.isValid() ? stroke : QColor(Qt::white);

    if (!strokeOnLight.isValid())
        strokeOnLight = stroke.isValid() ? stroke : QColor(Qt::black);

    shape->setColorOnDarkBackground(strokeOnDark);
    shape->setColorOnLightBackground(strokeOnLight);

    shape->setFilled(attributes.value(ns, "filled").toString() == trueString);
    shape->setFillOnDarkBackground(colorFromString(attributes.value(ns, "fill-on-dark-background").toString()));
    shape->setFillOnLightBackground(colorFromString(attributes.value(ns, "fill-on-light-background").toString()));

    if (shape->isLinear())
    {
        const auto startArrow = attributes.value(ns, "start-arrow");
        const auto endArrow = attributes.value(ns, "end-arrow");

        shape->setArrowHeads(startArrow.isNull() ? false : startArrow.toString() == trueString,
                             endArrow.isNull() ? shape->kind() == UBGraphicsShapeItem::Arrow : endArrow.toString() == trueString);
    }

    // geometry in item coordinates
    QRectF rect;

    if (shape->isLinear())
    {
        QLineF line;

        if (name == "line")
        {
            line = QLineF(attributeValue(attributes, "x1"), attributeValue(attributes, "y1"),
                          attributeValue(attributes, "x2"), attributeValue(attributes, "y2"));
        }
        else
        {
            line = QLineF(ubAttributeValue(attributes, ns, "x1"), ubAttributeValue(attributes, ns, "y1"),
                          ubAttributeValue(attributes, ns, "x2"), ubAttributeValue(attributes, ns, "y2"));
        }

        rect = QRectF(line.p1(), line.p2()).normalized();
        shape->setLineDirection(line.x1() <= line.x2(), line.y1() <= line.y2());
    }
    else if (name == "ellipse")
    {
        const qreal cx = attributeValue(attributes, "cx");
        const qreal cy = attributeValue(attributes, "cy");
        const qreal rx = attributeValue(attributes, "rx");
        const qreal ry = attributeValue(attributes, "ry");
        rect = QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry);
    }
    else if (name == "polygon")
    {
        rect = pointsFromString(attributes.value("points").toString()).boundingRect();
    }
    else
    {
        rect = QRectF(attributeValue(attributes, "x"), attributeValue(attributes, "y"),
                      attributeValue(attributes, "width"), attributeValue(attributes, "height"));
    }

    shape->setBoxSize(rect.size());

    if (!rect.topLeft().isNull())
    {
        // the box of the shape always starts at the item origin
        shape->setTransform(QTransform::fromTranslate(rect.x(), rect.y()) * shape->transform());
    }
}

void UBShapeSvgSerializer::writeShape(QXmlStreamWriter& writer, const QString& namespaceUri, const UBGraphicsShapeItem* shape,
                                      bool darkBackground, bool isBackground)
{
    const QString& ns = namespaceUri;
    const QSizeF size = shape->boxSize();
    const QColor strokeColor = shape->strokeColor(darkBackground);
    const bool hasArrows = shape->isLinear() && (shape->hasStartArrow() || shape->hasEndArrow());

    switch (shape->kind())
    {
    case UBGraphicsShapeItem::Rectangle:
    case UBGraphicsShapeItem::RoundedRectangle:
        writer.writeStartElement("rect");
        writer.writeAttribute("x", "0");
        writer.writeAttribute("y", "0");
        writer.writeAttribute("width", number(size.width()));
        writer.writeAttribute("height", number(size.height()));

        if (shape->kind() == UBGraphicsShapeItem::RoundedRectangle)
        {
            writer.writeAttribute("rx", number(shape->cornerRadius()));
            writer.writeAttribute("ry", number(shape->cornerRadius()));
        }
        break;

    case UBGraphicsShapeItem::Ellipse:
        writer.writeStartElement("ellipse");
        writer.writeAttribute("cx", number(size.width() / 2));
        writer.writeAttribute("cy", number(size.height() / 2));
        writer.writeAttribute("rx", number(size.width() / 2));
        writer.writeAttribute("ry", number(size.height() / 2));
        break;

    case UBGraphicsShapeItem::Triangle:
        writer.writeStartElement("polygon");
        writer.writeAttribute("points", pointsToString(shape->trianglePolygon()));
        break;

    case UBGraphicsShapeItem::Line:
    case UBGraphicsShapeItem::Arrow:
    default:
    {
        const QLineF line = shape->line();

        if (hasArrows)
        {
            // line and filled arrow heads in a single path. The line has no area,
            // so the fill only applies to the heads.
            const QLineF visible = shape->visibleLine();
            QString d = QString("M %1 %2 L %3 %4").arg(number(visible.x1()), number(visible.y1()), number(visible.x2()), number(visible.y2()));

            if (shape->hasStartArrow())
                d += " " + polygonPath(shape->arrowHead(line.p1(), line.p2()));

            if (shape->hasEndArrow())
                d += " " + polygonPath(shape->arrowHead(line.p2(), line.p1()));

            writer.writeStartElement("path");
            writer.writeAttribute("d", d);
            writer.writeAttribute(ns, "x1", number(line.x1()));
            writer.writeAttribute(ns, "y1", number(line.y1()));
            writer.writeAttribute(ns, "x2", number(line.x2()));
            writer.writeAttribute(ns, "y2", number(line.y2()));
        }
        else
        {
            writer.writeStartElement("line");
            writer.writeAttribute("x1", number(line.x1()));
            writer.writeAttribute("y1", number(line.y1()));
            writer.writeAttribute("x2", number(line.x2()));
            writer.writeAttribute("y2", number(line.y2()));
        }
        break;
    }
    }

    // presentation attributes
    writer.writeAttribute("stroke", strokeColor.name());

    if (strokeColor.alpha() != 255)
        writer.writeAttribute("stroke-opacity", number(strokeColor.alphaF()));

    writer.writeAttribute("stroke-width", number(shape->strokeWidth()));
    writer.writeAttribute("stroke-linecap", "round");
    writer.writeAttribute("stroke-linejoin", "round");

    if (shape->isLinear())
    {
        writer.writeAttribute("fill", hasArrows ? strokeColor.name() : QString("none"));
    }
    else if (shape->isFilled())
    {
        const QColor fill = shape->fillColor(darkBackground);
        writer.writeAttribute("fill", fill.name());
        writer.writeAttribute("fill-opacity", number(fill.alphaF()));
    }
    else
    {
        writer.writeAttribute("fill", "none");
    }

    writer.writeAttribute("transform", transformToString(shape->sceneTransform()));

    // OpenBoard attributes
    writer.writeAttribute(ns, "type", "shape");
    writer.writeAttribute(ns, "kind", UBGraphicsShapeItem::kindToString(shape->kind()));
    writer.writeAttribute(ns, "uuid", UBStringUtils::toCanonicalUuid(shape->uuid()));

    QString zs;
    zs.setNum(shape->zValue(), 'f'); // 'f' keeps precision
    writer.writeAttribute(ns, "z-value", zs);
    writer.writeAttribute(ns, "background", isBackground ? trueString : falseString);
    writer.writeAttribute(ns, "layer", QString::number(shape->data(UBGraphicsItemData::ItemLayerType).toInt()));

    if (shape->data(UBGraphicsItemData::ItemLocked).toBool())
        writer.writeAttribute(ns, "locked", trueString);

    writer.writeAttribute(ns, "hidden-on-display", shape->data(UBGraphicsItemData::ItemIsHiddenOnDisplay).toBool() ? trueString : falseString);

    writer.writeAttribute(ns, "stroke-on-dark-background", colorToString(shape->colorOnDarkBackground()));
    writer.writeAttribute(ns, "stroke-on-light-background", colorToString(shape->colorOnLightBackground()));

    if (shape->isFillable())
    {
        writer.writeAttribute(ns, "filled", shape->isFilled() ? trueString : falseString);

        if (shape->fillOnDarkBackground().isValid())
            writer.writeAttribute(ns, "fill-on-dark-background", colorToString(shape->fillOnDarkBackground()));

        if (shape->fillOnLightBackground().isValid())
            writer.writeAttribute(ns, "fill-on-light-background", colorToString(shape->fillOnLightBackground()));
    }
    else
    {
        writer.writeAttribute(ns, "start-arrow", shape->hasStartArrow() ? trueString : falseString);
        writer.writeAttribute(ns, "end-arrow", shape->hasEndArrow() ? trueString : falseString);
    }

    writer.writeEndElement();
}
