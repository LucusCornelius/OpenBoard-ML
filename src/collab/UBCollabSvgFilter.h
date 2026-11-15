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

#ifndef UBCOLLABSVGFILTER_H_
#define UBCOLLABSVGFILTER_H_

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * Cleans the text of a page (as UBSvgSubsetAdaptor writes it) that comes from
 * another computer, before the page reader sees it. The page is read and
 * written again with only what may be shared: strokes, pictures and svg
 * pictures from the images folder of the document, texts, shapes and groups;
 * for whole pages also PDF pages from the objects folder and the background
 * ruling. Everything else is left out: widgets, web content, audio and video,
 * the drawing tools, unknown elements, scripts, document type definitions.
 * File references must be plain names in those folders; numbers must be
 * finite and of a sane size; the size of the text, the number of elements and
 * the number of points are limited.
 */
class UBCollabSvgFilter
{
    public:
        enum Mode
        {
            Fragment,   // items sent during the session
            Page        // a whole page of the initial board
        };

        struct Result
        {
            QByteArray svg;         // the cleaned page; empty when refused
            QStringList assets;     // the files it refers to, relative to the document
            int dropped{0};         // elements that were left out
            QString error;          // why the page was refused

            bool isValid() const { return error.isEmpty(); }
        };

        static Result filter(const QByteArray& svg, Mode mode);

        /** a file of the document an item may refer to: a plain name in images/ (and objects/ for PDFs of a page) */
        static bool isSafeAssetPath(const QString& path, Mode mode);

        /** the html of a text item without images and external references */
        static QString cleanTextHtml(const QString& html);
};

#endif /* UBCOLLABSVGFILTER_H_ */
