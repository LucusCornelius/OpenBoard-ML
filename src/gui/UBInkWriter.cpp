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

#include "UBInkWriter.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QWidget>

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsStrokesGroup.h"

#include "core/memcheck.h"

namespace
{
    // the size slider gives the height of capitals and digits
    const qreal sCapHeight = 0.66;

    QRectF inkBounds(const QList<QPolygonF>& ink, const UBMathInkLayout::Formula& formula)
    {
        QRectF rect;

        for (const QPolygonF& stroke : ink)
        {
            rect = rect.isNull() ? stroke.boundingRect() : rect.united(stroke.boundingRect());
        }

        for (const QRectF& placeholder : formula.placeholders)
        {
            rect = rect.isNull() ? placeholder : rect.united(placeholder);
        }

        return rect;
    }
}

/** Transparent overlay on the board viewport showing where the formula will be written. */
class UBInkGhost : public QWidget
{
    public:
        explicit UBInkGhost(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_NoSystemBackground);
            setAttribute(Qt::WA_TranslucentBackground);
            setFocusPolicy(Qt::NoFocus);
        }

        void setPaths(const QPainterPath& ink, const QPainterPath& placeholders)
        {
            mInk = ink;
            mPlaceholders = placeholders;
            update();
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            QPen placeholderPen(QColor(128, 128, 128, 150), 1, Qt::DashLine);
            painter.setPen(placeholderPen);
            painter.drawPath(mPlaceholders);

            QPen inkPen(QColor(110, 110, 110, 170), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(inkPen);
            painter.drawPath(mInk);
        }

    private:
        QPainterPath mInk;
        QPainterPath mPlaceholders;
};

UBInkWriter::UBInkWriter(QObject* parent)
    : QObject(parent)
{
    connect(UBDrawingController::drawingController(), &UBDrawingController::stylusToolChanged,
            this, &UBInkWriter::cancelPlacement);
}

UBInkWriter::~UBInkWriter()
{
    if (mFilterInstalled)
    {
        qApp->removeEventFilter(this);
    }

    delete mGhost;
}

qreal UBInkWriter::emSizeInScene()
{
    qreal centimetre = 28;

    if (UBApplication::boardController && UBApplication::boardController->activeScene())
    {
        centimetre = qMax(4, UBApplication::boardController->activeScene()->backgroundGridSize());
    }

    const qreal sizeCm = qBound(1.0, UBSettings::settings()->boardMathInkSizeCm->get().toDouble(), 6.0);
    return sizeCm * centimetre / sCapHeight;
}

QList<QPolygonF> UBInkWriter::inkStrokes(const UBMathInkLayout::Formula& formula)
{
    bool ok = false;
    qreal jitter = UBSettings::settings()->boardMathInkJitter->get().toDouble(&ok);

    if (!ok)
    {
        jitter = 1.0;
    }

    return UBMathInkLayout::humanize(formula, jitter);
}

bool UBInkWriter::write(const UBMathInkLayout::Formula& formula, const QPointF& scenePos)
{
    UBBoardController* controller = UBApplication::boardController;

    if (!controller || !controller->activeScene() || formula.isEmpty())
    {
        return false;
    }

    std::shared_ptr<UBGraphicsScene> scene = controller->activeScene();
    const QList<QPolygonF> ink = inkStrokes(formula);
    const QPointF center = inkBounds(ink, formula).center();
    const qreal em = emSizeInScene();

    QList<QPolygonF> sceneStrokes;

    for (const QPolygonF& stroke : ink)
    {
        QPolygonF mapped;

        for (int i = 0; i < stroke.size(); ++i)
        {
            const QPointF q = scenePos + (stroke.at(i) - center) * em;

            // drop points that are too close to each other, but keep the end point
            if (mapped.isEmpty() || QLineF(mapped.last(), q).length() > 0.6)
            {
                mapped << q;
            }
            else if (i == stroke.size() - 1 && mapped.size() > 1)
            {
                mapped.last() = q;
            }
        }

        if (mapped.size() == 1)
        {
            mapped << mapped.first() + QPointF(0.5, 0);
        }

        sceneStrokes << mapped;
    }

    // the pen width, as for a stroke drawn with the pen at the current zoom
    qreal width = UBSettings::settings()->currentPenWidth();
    width /= controller->systemScaleFactor();
    width /= controller->currentZoom();

    return scene->addInkStrokes(sceneStrokes, width, controller->penColorOnLightBackground(),
                                controller->penColorOnDarkBackground()) != nullptr;
}

QWidget* UBInkWriter::boardViewport() const
{
    if (UBApplication::boardController && UBApplication::boardController->controlView())
    {
        return UBApplication::boardController->controlView()->viewport();
    }

    return nullptr;
}

void UBInkWriter::beginPlacement(const UBMathInkLayout::Formula& formula)
{
    if (mPlacing)
    {
        endPlacement(false);
    }

    mViewport = boardViewport();

    if (!mViewport || formula.isEmpty())
    {
        return;
    }

    // another page: the formula is not placed there (the board controller may not exist yet in the constructor)
    connect(UBApplication::boardController, &UBBoardController::activeSceneChanged,
            this, &UBInkWriter::cancelPlacement, Qt::UniqueConnection);

    mFormula = formula;
    mInk = inkStrokes(formula);
    mInkBounds = inkBounds(mInk, formula);
    mPlacing = true;
    mSwallowRelease = false;

    if (!mFilterInstalled)
    {
        qApp->installEventFilter(this);
        mFilterInstalled = true;
    }

    if (UBApplication::boardController->activeScene())
    {
        UBApplication::boardController->activeScene()->hideTool();
    }

    if (!mGhost || mGhost->parentWidget() != mViewport)
    {
        delete mGhost;
        mGhost = new UBInkGhost(mViewport);
    }

    mGhost->setGeometry(mViewport->rect());
    mGhost->setPaths(QPainterPath(), QPainterPath());
    mGhost->show();
    mGhost->raise();
    mViewport->setCursor(Qt::CrossCursor);

    const QPoint cursorPos = mViewport->mapFromGlobal(QCursor::pos());

    if (mViewport->rect().contains(cursorPos))
    {
        updateGhost(cursorPos);
    }

    emit placementStarted();
}

void UBInkWriter::cancelPlacement()
{
    if (mPlacing)
    {
        endPlacement(false);
    }
}

void UBInkWriter::endPlacement(bool written)
{
    const bool wasPlacing = mPlacing;
    mPlacing = false;

    if (mGhost)
    {
        mGhost->hide();
    }

    if (mFilterInstalled && !mSwallowRelease)
    {
        qApp->removeEventFilter(this);
        mFilterInstalled = false;
    }

    if (wasPlacing && UBApplication::boardController && UBApplication::boardController->controlView())
    {
        // back to the cursor of the current tool
        UBApplication::boardController->controlView()->setToolCursor(UBDrawingController::drawingController()->stylusTool());
    }

    if (wasPlacing)
    {
        emit placementFinished(written);
    }
}

void UBInkWriter::updateGhost(const QPoint& viewportPos)
{
    UBBoardView* view = UBApplication::boardController ? UBApplication::boardController->controlView() : nullptr;

    if (!mGhost || !view)
    {
        return;
    }

    const QPointF scenePos = view->mapToScene(viewportPos);
    const qreal em = emSizeInScene();
    const QPointF center = mInkBounds.center();

    const QTransform viewportTransform = view->viewportTransform();

    auto toView = [&](const QPointF& p) {
        return viewportTransform.map(scenePos + (p - center) * em);
    };

    QPainterPath ink;

    for (const QPolygonF& stroke : mInk)
    {
        for (int i = 0; i < stroke.size(); ++i)
        {
            if (i == 0)
            {
                ink.moveTo(toView(stroke.at(i)));
            }
            else
            {
                ink.lineTo(toView(stroke.at(i)));
            }
        }
    }

    QPainterPath placeholders;

    for (const QRectF& rect : mFormula.placeholders)
    {
        placeholders.addPolygon(QPolygonF({toView(rect.topLeft()), toView(rect.topRight()),
                                           toView(rect.bottomRight()), toView(rect.bottomLeft()), toView(rect.topLeft())}));
    }

    if (mGhost->geometry() != mViewport->rect())
    {
        mGhost->setGeometry(mViewport->rect());
    }

    mGhost->setPaths(ink, placeholders);
}

bool UBInkWriter::eventFilter(QObject* watched, QEvent* event)
{
    if (mSwallowRelease)
    {
        // eat the release that belongs to the placing press
        if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::TabletRelease)
        {
            mSwallowRelease = false;

            if (!mPlacing && mFilterInstalled)
            {
                qApp->removeEventFilter(this);
                mFilterInstalled = false;
            }

            event->accept();
            return true;
        }

        if (event->type() == QEvent::MouseMove || event->type() == QEvent::TabletMove)
        {
            return watched == mViewport;
        }
    }

    if (!mPlacing)
    {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type())
    {
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
        {
            endPlacement(false);
            return true;
        }
        break;

    case QEvent::MouseMove:
        if (watched == mViewport)
        {
            updateGhost(static_cast<QMouseEvent*>(event)->position().toPoint());
            return true;
        }
        break;

    case QEvent::TabletMove:
        if (watched == mViewport)
        {
            updateGhost(static_cast<QTabletEvent*>(event)->position().toPoint());
            event->accept();
            return true;
        }
        break;

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TabletPress:
        if (watched == mViewport)
        {
            QPointF pos;
            bool place = true;

            if (event->type() == QEvent::TabletPress)
            {
                pos = static_cast<QTabletEvent*>(event)->position();
            }
            else
            {
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                pos = mouseEvent->position();
                place = mouseEvent->button() == Qt::LeftButton;
            }

            mSwallowRelease = true;
            bool written = false;

            if (place && UBApplication::boardController && UBApplication::boardController->controlView())
            {
                written = write(mFormula, UBApplication::boardController->controlView()->mapToScene(pos.toPoint()));
            }

            endPlacement(written);
            event->accept();
            return true;
        }
        break;

    case QEvent::Leave:
        if (watched == mViewport && mGhost)
        {
            mGhost->setPaths(QPainterPath(), QPainterPath());
        }
        break;

    case QEvent::Hide:
        if (watched == mViewport)
        {
            endPlacement(false);
        }
        break;

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}
