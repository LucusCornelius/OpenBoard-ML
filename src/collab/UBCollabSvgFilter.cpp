/*
 * Copyright (C) 2026 OpenBoard-ML contributors
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

#include "UBCollabSvgFilter.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

#include "core/UBSettings.h"

#include "UBCollabProtocol.h"

#include "core/memcheck.h"

namespace
{
    const QString nsSvg = "http://www.w3.org/2000/svg";
    const QString nsXLink = "http://www.w3.org/1999/xlink";
    const QString nsXHtml = "http://www.w3.org/1999/xhtml";

    // the largest coordinate, size or z value accepted
    constexpr double maxNumber = 1.0e8;

    constexpr int maxDepth = 24;
    constexpr qsizetype maxAttributeBytes = 64 * 1024;
    constexpr qsizetype maxPointsAttributeBytes = 2 * 1024 * 1024;
    constexpr qsizetype maxTextBytes = 4 * 1024 * 1024;
    constexpr int maxAttributes = 64;
    constexpr int maxPointsPerElement = 20000;

    struct Limits
    {
        qint64 elements;
        qint64 points;
    };

    Limits limitsFor(UBCollabSvgFilter::Mode mode)
    {
        if (mode == UBCollabSvgFilter::Page)
            return {2000000, 10000000};

        return {200000, 1000000};
    }

    QString ubNs()
    {
        return UBSettings::uniboardDocumentNamespaceUri;
    }

    /** a text document that counts the files a text asks for and fetches none of them */
    class ProbeDocument : public QTextDocument
    {
        public:
            int requests() const
            {
                return mRequests;
            }

            bool holdsImages() const
            {
                for (QTextBlock block = begin(); block.isValid(); block = block.next())
                {
                    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
                    {
                        if (it.fragment().charFormat().isImageFormat())
                            return true;
                    }
                }

                return false;
            }

        protected:
            QVariant loadResource(int type, const QUrl& name) override
            {
                Q_UNUSED(type);
                Q_UNUSED(name);

                ++mRequests;
                return QVariant();
            }

        private:
            int mRequests{0};
    };

    bool isNumericAttribute(const QString& ns, const QString& name)
    {
        static const QSet<QString> plain = {"transform", "x", "y", "width", "height", "x1", "y1", "x2", "y2",
                                            "cx", "cy", "rx", "ry", "r", "points", "d", "stroke-width", "viewBox",
                                            "fill-opacity", "stroke-opacity", "opacity", "pageDpi"};
        static const QSet<QString> ub = {"z-value", "width", "height", "pixels-per-point", "grid-size",
                                         "stroke-width", "corner-radius", "radius"};

        if (ns.isEmpty())
            return plain.contains(name);

        if (ns == ubNs())
            return ub.contains(name);

        return false;
    }

    /**
     * Every number in the value is finite and not absurdly large; counts the
     * numbers. Values like "nan", "inf" or "1e999" are refused.
     */
    bool saneNumbers(const QString& value, int* count)
    {
        const QString lower = value.toLower();

        if (lower.contains("nan") || lower.contains("inf"))
            return false;

        int numbers = 0;
        const int length = int(value.size());
        int i = 0;

        while (i < length)
        {
            const QChar c = value.at(i);

            if (c.isDigit() || ((c == '.' || c == '-' || c == '+') && i + 1 < length
                                && (value.at(i + 1).isDigit() || value.at(i + 1) == '.')))
            {
                int end = i + 1;

                while (end < length)
                {
                    const QChar d = value.at(end);

                    if (d.isDigit() || d == '.')
                    {
                        ++end;
                    }
                    else if ((d == 'e' || d == 'E') && end + 1 < length
                             && (value.at(end + 1).isDigit() || value.at(end + 1) == '-' || value.at(end + 1) == '+'))
                    {
                        end += 2;
                    }
                    else
                    {
                        break;
                    }
                }

                bool ok = false;
                const double number = value.mid(i, end - i).toDouble(&ok);

                // a token like "1.2.3" is not a number for the reader either: its own parsing decides
                if (ok && (!std::isfinite(number) || std::fabs(number) > maxNumber))
                    return false;

                ++numbers;
                i = end;
            }
            else
            {
                ++i;
            }
        }

        if (count)
            *count = numbers;

        return true;
    }

    class Filter
    {
        public:
            Filter(const QByteArray& input, UBCollabSvgFilter::Mode mode)
                : mReader(input)
                , mMode(mode)
                , mLimits(limitsFor(mode))
            {
                mBuffer.open(QIODevice::WriteOnly);
                mWriter.setDevice(&mBuffer);
            }

            UBCollabSvgFilter::Result run()
            {
                UBCollabSvgFilter::Result result;

                if (!readRoot())
                {
                    result.error = mError.isEmpty() ? QString("not a page") : mError;
                    return result;
                }

                mWriter.writeEndDocument();

                if (mReader.hasError() && mError.isEmpty())
                    mError = mReader.errorString();

                if (!mError.isEmpty())
                {
                    result.error = mError;
                    return result;
                }

                result.svg = mBuffer.data();
                result.assets = mAssets;
                result.dropped = mDropped;
                return result;
            }

        private:
            bool fail(const QString& reason)
            {
                if (mError.isEmpty())
                    mError = reason;

                return false;
            }

            bool countElement()
            {
                if (++mElements > mLimits.elements)
                    return fail("too many elements");

                return true;
            }

            /** next token that matters; false at the end of the parent or on an error */
            bool nextChild()
            {
                while (!mReader.atEnd())
                {
                    switch (mReader.readNext())
                    {
                        case QXmlStreamReader::StartElement:
                            return true;

                        case QXmlStreamReader::EndElement:
                            return false;

                        case QXmlStreamReader::DTD:
                        case QXmlStreamReader::EntityReference:
                            fail("document type definitions are not allowed");
                            return false;

                        case QXmlStreamReader::Invalid:
                            fail(mReader.errorString());
                            return false;

                        default:
                            // characters, comments, processing instructions: left out
                            break;
                    }
                }

                return false;
            }

            void skip()
            {
                ++mDropped;
                mReader.skipCurrentElement();
            }

            bool readRoot()
            {
                while (!mReader.atEnd())
                {
                    const QXmlStreamReader::TokenType token = mReader.readNext();

                    if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
                        return fail("document type definitions are not allowed");

                    if (token == QXmlStreamReader::Invalid)
                        return fail(mReader.errorString());

                    if (token == QXmlStreamReader::StartElement)
                        break;
                }

                if (!mReader.isStartElement() || mReader.name() != QLatin1String("svg") || mReader.namespaceUri() != nsSvg)
                    return fail("the root is not an svg element");

                countElement();

                mWriter.writeStartDocument();
                mWriter.writeDefaultNamespace(nsSvg);
                mWriter.writeNamespace(nsXLink, "xlink");
                mWriter.writeNamespace(ubNs(), "ub");
                mWriter.writeNamespace(nsXHtml, "xhtml");
                mWriter.writeStartElement(nsSvg, "svg");
                writeRootAttributes();

                while (mError.isEmpty() && nextChild())
                {
                    if (!countElement())
                        break;

                    topLevelElement();
                }

                mWriter.writeEndElement();
                return mError.isEmpty();
            }

            void writeRootAttributes()
            {
                const QXmlStreamAttributes attributes = mReader.attributes();

                mWriter.writeAttribute("version", "1.1");
                // the content is read as the current file version, whatever the sender claims
                mWriter.writeAttribute(ubNs(), "version", UBSettings::currentFileVersion);

                const QUuid uuid(attributes.value(ubNs(), "uuid").toString());

                if (!uuid.isNull())
                    mWriter.writeAttribute(ubNs(), "uuid", uuid.toString(QUuid::WithoutBraces));

                const QString viewBox = attributes.value("viewBox").toString();
                int numbers = 0;

                if (!viewBox.isEmpty() && viewBox.size() < 200 && saneNumbers(viewBox, &numbers) && numbers == 4)
                    mWriter.writeAttribute("viewBox", viewBox);

                auto flag = [&](const QString& name) {
                    const QString value = attributes.value(ubNs(), name).toString();

                    if (value == QLatin1String("true") || value == QLatin1String("false"))
                        mWriter.writeAttribute(ubNs(), name, value);
                };

                // how the colours of the items were meant
                flag("dark-background");

                if (mMode != UBCollabSvgFilter::Page)
                    return;

                flag("crossed-background");
                flag("ruled-background");

                const QString size = attributes.value(ubNs(), "nominal-size").toString();
                const QStringList sides = size.split('x');

                if (sides.size() == 2)
                {
                    bool okWidth = false;
                    bool okHeight = false;
                    const int width = sides.at(0).toInt(&okWidth);
                    const int height = sides.at(1).toInt(&okHeight);

                    if (okWidth && okHeight && width > 0 && height > 0
                            && width <= UBCollabProtocol::maxPageSide && height <= UBCollabProtocol::maxPageSide)
                    {
                        mWriter.writeAttribute(ubNs(), "nominal-size", QString("%1x%2").arg(width).arg(height));
                    }
                }

                bool ok = false;
                const int grid = attributes.value(ubNs(), "grid-size").toInt(&ok);

                if (ok && grid > 0 && grid <= UBCollabProtocol::maxGridSize)
                    mWriter.writeAttribute(ubNs(), "grid-size", QString::number(grid));

                const int lines = attributes.value(ubNs(), "intermediate-lines").toInt(&ok);

                if (ok && (lines == 0 || lines == 1))
                    mWriter.writeAttribute(ubNs(), "intermediate-lines", QString::number(lines));

                const int dpi = attributes.value("pageDpi").toInt(&ok);

                if (ok && dpi >= 10 && dpi <= 2400)
                    mWriter.writeAttribute("pageDpi", QString::number(dpi));
            }

            void topLevelElement()
            {
                const QString name = mReader.name().toString();

                if (mReader.namespaceUri() == nsSvg)
                {
                    const bool shape = mReader.attributes().value(ubNs(), "type") == QLatin1String("shape");

                    if (name == "g")
                    {
                        strokeGroup();
                        return;
                    }

                    if (name == "polygon" || name == "line" || name == "polyline"
                            || ((name == "rect" || name == "ellipse" || name == "path") && shape))
                    {
                        leaf(name);
                        return;
                    }

                    if (name == "image")
                    {
                        image();
                        return;
                    }

                    if (name == "foreignObject")
                    {
                        foreignObject();
                        return;
                    }

                    if (name == "groups")
                    {
                        groups();
                        return;
                    }
                }

                if (name == "background" && mMode == UBCollabSvgFilter::Page)
                {
                    copySubtree(0);
                    return;
                }

                // the page fill rectangle, tools, widgets, media and anything unknown
                skip();
            }

            /** writes the attributes that are allowed, false when one of them is refused */
            bool writeAttributes(const QString& element, bool hrefChecked = false)
            {
                const QXmlStreamAttributes attributes = mReader.attributes();

                if (attributes.size() > maxAttributes)
                    return fail("too many attributes on " + element);

                for (const QXmlStreamAttribute& attribute : attributes)
                {
                    const QString ns = attribute.namespaceUri().toString();
                    const QString name = attribute.name().toString();
                    const QString value = attribute.value().toString();

                    // event handlers mean scripts; nothing here reads them, but they are not kept
                    if (name.startsWith("on", Qt::CaseInsensitive))
                        continue;

                    if (!ns.isEmpty() && ns != ubNs() && ns != nsXLink)
                        continue;

                    if (ns == nsXLink)
                    {
                        // only the reference of a picture or a PDF page, checked by the caller
                        if (!hrefChecked || name != "href")
                            continue;
                    }

                    const bool points = (name == "points" || name == "d") && ns.isEmpty();

                    if (value.size() > (points ? maxPointsAttributeBytes : maxAttributeBytes))
                        return fail("attribute too long on " + element);

                    if (isNumericAttribute(ns, name))
                    {
                        int numbers = 0;

                        if (!saneNumbers(value, &numbers))
                            return fail(QString("invalid number in %1 of %2").arg(name, element));

                        if (points)
                        {
                            const int pointCount = numbers / 2;

                            if (pointCount > maxPointsPerElement)
                                return fail("too many points in " + element);

                            mPoints += pointCount;

                            if (mPoints > mLimits.points)
                                return fail("too many points");
                        }
                    }

                    // the reader would make a background object of it: never from another computer
                    if (ns == ubNs() && name == "background" && mMode == UBCollabSvgFilter::Fragment)
                    {
                        mWriter.writeAttribute(ubNs(), "background", "false");
                        continue;
                    }

                    if (ns.isEmpty())
                        mWriter.writeAttribute(name, value);
                    else
                        mWriter.writeAttribute(ns, name, value);
                }

                return true;
            }

            /** an element without content of its own; anything inside it is left out */
            void leaf(const QString& name)
            {
                mWriter.writeStartElement(nsSvg, name);

                if (writeAttributes(name))
                {
                    while (mError.isEmpty() && nextChild())
                        skip();
                }

                mWriter.writeEndElement();
            }

            void strokeGroup()
            {
                mWriter.writeStartElement(nsSvg, "g");

                if (!writeAttributes("g"))
                    return;

                while (mError.isEmpty() && nextChild())
                {
                    if (!countElement())
                        return;

                    const QString name = mReader.name().toString();

                    if (mReader.namespaceUri() == nsSvg && (name == "polygon" || name == "line" || name == "polyline"))
                        leaf(name);
                    else
                        skip();
                }

                mWriter.writeEndElement();
            }

            void image()
            {
                const QString href = mReader.attributes().value(nsXLink, "href").toString();

                if (!UBCollabSvgFilter::isSafeAssetPath(href, UBCollabSvgFilter::Fragment))
                {
                    // a file outside the folder of pictures of the document: not read
                    skip();
                    return;
                }

                if (!mAssets.contains(href))
                    mAssets << href;

                mWriter.writeStartElement(nsSvg, "image");

                if (writeAttributes("image", true))
                {
                    while (mError.isEmpty() && nextChild())
                        skip();
                }

                mWriter.writeEndElement();
            }

            void foreignObject()
            {
                const QXmlStreamAttributes attributes = mReader.attributes();

                if (attributes.value(ubNs(), "type") == QLatin1String("text"))
                {
                    textObject();
                    return;
                }

                const QString href = attributes.value(nsXLink, "href").toString();

                if (mMode == UBCollabSvgFilter::Page && href.contains("#page="))
                {
                    const QString file = href.section("#page=", 0, 0);
                    bool ok = false;
                    const int page = href.section("#page=", 1).toInt(&ok);

                    if (ok && page > 0 && page < 100000 && UBCollabSvgFilter::isSafeAssetPath(file, UBCollabSvgFilter::Page)
                            && file.endsWith(".pdf", Qt::CaseInsensitive))
                    {
                        if (!mAssets.contains(file))
                            mAssets << file;

                        mWriter.writeStartElement(nsSvg, "foreignObject");

                        if (writeAttributes("foreignObject", true))
                        {
                            while (mError.isEmpty() && nextChild())
                                skip();
                        }

                        mWriter.writeEndElement();
                        return;
                    }
                }

                // widgets and web content
                skip();
            }

            void textObject()
            {
                mWriter.writeStartElement(nsSvg, "foreignObject");

                if (!writeAttributes("foreignObject"))
                    return;

                while (mError.isEmpty() && nextChild())
                {
                    if (!countElement())
                        return;

                    if (mReader.name() == QLatin1String("itemTextContent"))
                    {
                        const QString html = mReader.readElementText(QXmlStreamReader::SkipChildElements);

                        if (html.size() > maxTextBytes)
                        {
                            fail("text too long");
                            return;
                        }

                        mWriter.writeStartElement(nsSvg, "itemTextContent");
                        mWriter.writeCharacters(UBCollabSvgFilter::cleanTextHtml(html));
                        mWriter.writeEndElement();
                    }
                    else
                    {
                        skip();
                    }
                }

                mWriter.writeEndElement();
            }

            void groups()
            {
                mWriter.writeStartElement(nsSvg, "groups");

                while (mError.isEmpty() && nextChild())
                {
                    if (!countElement())
                        return;

                    if (mReader.name() == QLatin1String("group"))
                        group(1);
                    else
                        skip();
                }

                mWriter.writeEndElement();
            }

            void group(int depth)
            {
                if (depth > maxDepth)
                {
                    fail("groups nested too deep");
                    return;
                }

                mWriter.writeStartElement(nsSvg, "group");

                if (!writeAttributes("group"))
                    return;

                while (mError.isEmpty() && nextChild())
                {
                    if (!countElement())
                        return;

                    if (mReader.name() == QLatin1String("group"))
                    {
                        group(depth + 1);
                    }
                    else if (mReader.name() == QLatin1String("element"))
                    {
                        mWriter.writeStartElement(nsSvg, "element");

                        if (!writeAttributes("element"))
                            return;

                        while (mError.isEmpty() && nextChild())
                            skip();

                        mWriter.writeEndElement();
                    }
                    else
                    {
                        skip();
                    }
                }

                mWriter.writeEndElement();
            }

            /** the background ruling: a small tree of its own namespace, copied as it is */
            void copySubtree(int depth)
            {
                if (depth > maxDepth)
                {
                    fail("background nested too deep");
                    return;
                }

                const QString ns = mReader.namespaceUri().toString();
                mWriter.writeStartElement(ns, mReader.name().toString());

                const QXmlStreamAttributes attributes = mReader.attributes();

                if (attributes.size() > maxAttributes)
                {
                    fail("too many attributes in the background");
                    return;
                }

                for (const QXmlStreamAttribute& attribute : attributes)
                {
                    if (attribute.value().size() > maxAttributeBytes || attribute.name().startsWith(QLatin1String("on")))
                        continue;

                    if (attribute.namespaceUri().isEmpty())
                        mWriter.writeAttribute(attribute.name().toString(), attribute.value().toString());
                    else
                        mWriter.writeAttribute(attribute.namespaceUri().toString(), attribute.name().toString(), attribute.value().toString());
                }

                while (!mReader.atEnd() && mError.isEmpty())
                {
                    const QXmlStreamReader::TokenType token = mReader.readNext();

                    if (token == QXmlStreamReader::StartElement)
                    {
                        if (!countElement())
                            return;

                        copySubtree(depth + 1);
                    }
                    else if (token == QXmlStreamReader::Characters && !mReader.isWhitespace())
                    {
                        if (mReader.text().size() > maxAttributeBytes)
                        {
                            fail("background text too long");
                            return;
                        }

                        mWriter.writeCharacters(mReader.text().toString());
                    }
                    else if (token == QXmlStreamReader::EndElement)
                    {
                        break;
                    }
                    else if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
                    {
                        fail("document type definitions are not allowed");
                        return;
                    }
                    else if (token == QXmlStreamReader::Invalid)
                    {
                        fail(mReader.errorString());
                        return;
                    }
                }

                mWriter.writeEndElement();
            }

            QXmlStreamReader mReader;
            QBuffer mBuffer;
            QXmlStreamWriter mWriter;
            UBCollabSvgFilter::Mode mMode;
            Limits mLimits;
            qint64 mElements{0};
            qint64 mPoints{0};
            int mDropped{0};
            QStringList mAssets;
            QString mError;
    };
}


UBCollabSvgFilter::Result UBCollabSvgFilter::filter(const QByteArray& svg, Mode mode)
{
    const qsizetype limit = mode == Page ? UBCollabProtocol::maxPageBytes : UBCollabProtocol::maxFragmentBytes;

    if (svg.size() > limit)
    {
        Result result;
        result.error = "page too large";
        return result;
    }

    Filter filter(svg, mode);
    return filter.run();
}


bool UBCollabSvgFilter::isSafeAssetPath(const QString& path, Mode mode)
{
    if (path.isEmpty() || path.size() > UBCollabProtocol::maxAssetPathBytes)
        return false;

    const QString folder = path.section('/', 0, 0);
    const QString name = path.section('/', 1);

    // one folder level, a plain name: no "..", no absolute path, no drive, no backslash
    static const QRegularExpression plainName("^[A-Za-z0-9{][A-Za-z0-9._{}\\-]*$");

    if (name.isEmpty() || name.contains('/') || name.contains("..") || !plainName.match(name).hasMatch())
        return false;

    const QString suffix = name.section('.', -1).toLower();

    if (folder == "images")
    {
        static const QSet<QString> pictures = {"png", "jpg", "jpeg", "gif", "bmp", "svg", "webp", "tif", "tiff"};
        return pictures.contains(suffix) && name.contains('.');
    }

    if (folder == "objects" && mode == Page)
        return suffix == "pdf";

    return false;
}


QString UBCollabSvgFilter::cleanTextHtml(const QString& html)
{
    // pictures in a text would be loaded from a path chosen by the sender
    static const QRegularExpression image("<\\s*img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression url("url\\s*\\(", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression link("<\\s*link\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);

    QString clean = html;
    clean.remove(image);
    clean.remove(link);

    // no text can be read without a graphical application (and none is shown): the coarse pattern,
    // which also changes these letters where they are words of the text
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        clean.replace(url, "url-blocked(");
        return clean;
    }

    // patterns do not see what is written with character references or escapes, nor a background
    // attribute or an imported style sheet: the text is read the way the text item will read it,
    // and whatever it asks for is counted instead of fetched
    ProbeDocument probe;
    probe.setHtml(clean);

    if (probe.requests() == 0 && !probe.holdsImages())
        return clean;

    return Qt::convertFromPlainText(probe.toPlainText());
}


bool UBCollabSvgFilter::isSafeAssetContent(const QString& path, const QByteArray& data)
{
    const QString suffix = path.section('.', -1).toLower();

    if (suffix == "pdf")
        return true;

    if (suffix != "svg")
    {
        // a picture reader goes by the content when the suffix does not fit: a raster picture it
        // has to be, or an svg picture under another name would be read as one
        return data.startsWith("\x89PNG\r\n\x1a\n") || data.startsWith("\xff\xd8\xff")
                || data.startsWith("GIF87a") || data.startsWith("GIF89a") || data.startsWith("BM")
                || (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
                || data.startsWith(QByteArray("II*\0", 4)) || data.startsWith(QByteArray("MM\0*", 4));
    }

    // an svg picture is drawn with the pictures and style sheets it refers to, read from any path
    // (on Windows also from another computer): only what it carries itself is allowed. It is read
    // the way the renderer reads it; a compressed one is not readable here and refused.
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(false);

    while (!reader.atEnd())
    {
        switch (reader.readNext())
        {
            case QXmlStreamReader::ProcessingInstruction:
                return false;

            case QXmlStreamReader::StartElement:
            {
                const QXmlStreamAttributes attributes = reader.attributes();

                for (const QXmlStreamAttribute& attribute : attributes)
                {
                    const QStringView name = attribute.qualifiedName();

                    if (name != QLatin1String("href") && !name.endsWith(QLatin1String(":href")))
                        continue;

                    const QStringView value = attribute.value().trimmed();

                    if (!value.isEmpty() && !value.startsWith(QLatin1Char('#')) && !value.startsWith(QLatin1String("data:")))
                        return false;
                }
                break;
            }

            default:
                break;
        }
    }

    return !reader.hasError();
}
