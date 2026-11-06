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
    const qreal sPointsPerCentimetre = 72.0 / 2.54;

    /**
     * Curves are flattened this many times finer than one scene unit, so that the glyphs stay
     * smooth when the board is zoomed in.
     */
    const qreal sFlatteningScale = 4.0;
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
            // only the changed area, the whole viewport may be large
            const QRect before = (mInk.boundingRect() | mPlaceholders.boundingRect()).toAlignedRect().adjusted(-2, -2, 2, 2);
            mInk = ink;
            mPlaceholders = placeholders;
            update(before | (mInk.boundingRect() | mPlaceholders.boundingRect()).toAlignedRect().adjusted(-2, -2, 2, 2));
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            QPen placeholderPen(QColor(128, 128, 128, 150), 1, Qt::DashLine);
            painter.setPen(placeholderPen);
            painter.drawPath(mPlaceholders);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(110, 110, 110, 170));
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

int UBInkWriter::fontSize()
{
    bool ok = false;
    const int size = UBSettings::settings()->boardMathFontSize->get().toInt(&ok);
    return ok ? qBound(minimumFontSize, size, maximumFontSize) : defaultFontSize;
}

qreal UBInkWriter::emSizeInScene()
{
    qreal centimetre = 28;

    if (UBApplication::boardController && UBApplication::boardController->activeScene())
    {
        centimetre = qMax(4, UBApplication::boardController->activeScene()->backgroundGridSize());
    }

    return fontSize() / sPointsPerCentimetre * centimetre;
}

bool UBInkWriter::write(const UBMathInkLayout::Formula& formula, const QPointF& scenePos)
{
    UBBoardController* controller = UBApplication::boardController;

    if (!controller || !controller->activeScene() || formula.isEmpty())
    {
        return false;
    }

    std::shared_ptr<UBGraphicsScene> scene = controller->activeScene();
    const QPointF center = formula.bounds().center();
    const qreal em = emSizeInScene();

    // em units to scene units, the formula centred on the position
    QTransform toScene;
    toScene.translate(scenePos.x(), scenePos.y());
    toScene.scale(em, em);
    toScene.translate(-center.x(), -center.y());

    const QTransform fine = toScene * QTransform::fromScale(sFlatteningScale, sFlatteningScale);
    const QTransform back = QTransform::fromScale(1.0 / sFlatteningScale, 1.0 / sFlatteningScale);
    QList<QPolygonF> shapes;

    for (const QPainterPath& shape : formula.shapes)
    {
        // all contours of a glyph in one polygon (with zero width bridges between them)
        shapes << back.map(shape.toFillPolygon(fine));
    }

    return scene->addInkShapes(shapes, controller->penColorOnLightBackground(),
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
    mInk = formula.path();
    mInkBounds = formula.bounds();
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

    QTransform toView;
    toView.scale(em, em);
    toView.translate(-center.x(), -center.y());
    toView *= QTransform::fromTranslate(scenePos.x(), scenePos.y()) * viewportTransform;

    QPainterPath placeholders;

    for (const QRectF& rect : mFormula.placeholders)
    {
        placeholders.addPolygon(toView.map(QPolygonF(rect)));
        placeholders.closeSubpath();
    }

    const QPainterPath ink = toView.map(mInk);

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
