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

#ifndef UBCOLLABBOARDLINK_H_
#define UBCOLLABBOARDLINK_H_

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QUuid>

#include <memory>

class QWidget;
class UBBackgroundRuling;
class UBDocumentProxy;
class UBGraphicsScene;

/**
 * What the board sync needs to know about the board on the screen, in one
 * place: which page of a document the user works on, whether he is drawing
 * or dragging right now, and the few things that must follow on the board
 * when someone else changed the document. A shared document need not be on
 * the board at all: its pages are then changed through the scene cache only.
 *
 * It also watches the pointer on the board (without taking any event) for
 * the cursors the others see.
 */
class UBCollabBoardLink : public QObject
{
    Q_OBJECT

    public:
        static UBCollabBoardLink* instance();

        /** the scene the user edits, when the board shows this document; null otherwise */
        std::shared_ptr<UBGraphicsScene> editedScene(std::shared_ptr<UBDocumentProxy> document) const;

        /** the uuid of the page the board shows of this document; null when it shows another document */
        QUuid shownPage(std::shared_ptr<UBDocumentProxy> document) const;

        /** the board shows this document */
        bool showsDocument(std::shared_ptr<UBDocumentProxy> document) const;

        /** a press of the mouse, pen or finger on the board has not been released yet */
        bool isGestureActive() const { return mGestureActive; }

        /** shows a page of the document on the board (the board switches to the document if needed) */
        void showPage(std::shared_ptr<UBDocumentProxy> document, int index);

        /**
         * Pages were added, removed or moved by someone else: the board keeps
         * showing the page it showed, at its new place.
         */
        void pagesChanged(std::shared_ptr<UBDocumentProxy> document, const QUuid& shownBefore);

        /** the background of the page on the board was changed by someone else */
        void backgroundChanged(UBGraphicsScene* scene, bool dark, const UBBackgroundRuling* ruling, int gridSize);

        /** the size of the page on the board was changed by someone else */
        void pageSizeChanged(UBGraphicsScene* scene, const QSize& size);

        /** saves the page on the board, without touching the selection */
        void saveShownPage(std::shared_ptr<UBDocumentProxy> document);

        /** the widget the cursors of the others are drawn on */
        QWidget* boardViewport() const;

    signals:
        /** the press on the board was released */
        void gestureEnded();

        /** the pointer moved on the board (scene coordinates of the page shown) */
        void pointerMoved(const QPointF& scenePos, bool pressed);

        /** the pointer left the board */
        void pointerLeft();

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        explicit UBCollabBoardLink(QObject* parent);

        void watchBoard();
        void pointerEvent(const QPointF& viewportPos, bool pressed);

        QPointer<QWidget> mViewport;
        bool mGestureActive{false};
};

#endif /* UBCOLLABBOARDLINK_H_ */
