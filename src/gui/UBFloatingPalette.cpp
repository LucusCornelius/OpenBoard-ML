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




#include <QtGui>
#include <QPainterPath>
#include <QToolButton>
#include <QTimer>
#include <QAbstractScrollArea>

#include "UBFloatingPalette.h"
#include "UBGlassStyle.h"
#include "UBGlassEffect.h"
#include "UBModernStyle.h"
#include "UBUiStyle.h"

#include "frameworks/UBPlatformUtils.h"

#include "core/UBSettings.h"
#include "core/UBThemeManager.h"

#include "core/memcheck.h"

namespace
{
    // refresh period of the blurred backdrop
    constexpr int backdropThrottleMs = 40;
    // minimal period between two synchronous refreshes while dragging (about 60 fps)
    constexpr int backdropDragMs = 16;
    // blur radius of the backdrop in pixels (at 1x), the blur is done at a quarter of the resolution
    constexpr int backdropBlurRadius = 20;
    // light blur of the content seen through the rim (lensing), done at half resolution
    constexpr int lensBlurRadius = 3;
    // luminance thresholds (with hysteresis) for the dark material
    constexpr qreal darkBelow = 0.42;
    constexpr qreal lightAbove = 0.58;
}

int UBFloatingPalette::sRenderingBackdrop = 0;

/**
 * Watches the sibling widgets below a floating palette (board view, dock
 * palettes, other palettes) and requests a refresh of the blurred backdrop
 * when they repaint, move or change visibility in the covered area.
 * A separate object is used so that sub classes can keep their own event filters.
 */
class UBFloatingPaletteWatcher : public QObject
{
    public:
        explicit UBFloatingPaletteWatcher(UBFloatingPalette* palette)
            : QObject(palette)
            , mPalette(palette)
        {
        }

        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (watched->isWidgetType())
            {
                mPalette->siblingEvent(static_cast<QWidget*>(watched), event);
            }

            return false;
        }

    private:
        UBFloatingPalette* mPalette;
};

UBFloatingPalette::UBFloatingPalette(Qt::Corner position, QWidget *parent)
    : QWidget(parent, parent ? Qt::Widget : Qt::Tool | (Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint))
    , mCustomPosition(false)
    , mIsMoving(false)
    , mCanBeMinimized(false)
    , mIsClosable(false)
    , mMinimizedLocation(eMinimizedLocation_None)
    , mDefaultPosition(position)
    , mCloseButton(nullptr)
    , mGlassBackdropEnabled(true)
    , mShadowOnDark(false)
    , mOnDark(UBGlassStyle::isDark())
    , mBackdropTimer(nullptr)
    , mWatcher(nullptr)
{
    // glyphs and hover pills follow the material of the glass (see UBGlassStyle)
    setProperty("glassOnDark", mOnDark);
    UBGlassStyle::installMaterialTracker();

    connect(UBThemeManager::instance(), &UBThemeManager::themeChanged, this, [this]() {
        mShadow = QImage();
        updateMaterial();
        update();
    });

    setCursor(Qt::ArrowCursor);

    mWatcher = new UBFloatingPaletteWatcher(this);
    mBackdropTimer = new QTimer(this);
    mBackdropTimer->setSingleShot(true);
    mBackdropTimer->setInterval(backdropThrottleMs);
    connect(mBackdropTimer, &QTimer::timeout, this, [this]() {
        updateBackdrop();
    });

    if (parent)
    {
        setAttribute(Qt::WA_NoMousePropagation);
    }
    else
    {
        // standalone window
        // !!!! Should be included into Windows after QT recompilation
#ifndef Q_OS_WIN
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
#ifdef Q_OS_OSX
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        // didn't find the equivalent in Qt6
        #if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
            setAttribute(Qt::WA_MacNoShadow);
        #endif
#endif
    }

    mBackgroundBrush = QBrush(UBSettings::paletteColor);
    QPalette pal = palette();
    pal.setBrush(QPalette::Window, mBackgroundBrush);
    setPalette(pal);
    mbGrip = true;

    watchSiblings();
}

void UBFloatingPalette::setClosable(bool closable)
{
    mIsClosable = closable;

    if (mIsClosable)
    {
        buildCloseButton();
        mCloseButton->show();
        mCloseButton->raise();
    }
    else if (mCloseButton)
    {
        mCloseButton->hide();
    }

    update();
}

bool UBFloatingPalette::isClosable() const
{
    return mIsClosable;
}

void UBFloatingPalette::setGrip(bool newGrip)
{
    mbGrip = newGrip;
    update();
}


void UBFloatingPalette::setBackgroundBrush(const QBrush& brush)
{
    if (mBackgroundBrush != brush)
    {
        mBackgroundBrush = brush;
        QPalette pal = palette();
        pal.setBrush(QPalette::Window, mBackgroundBrush);
        setPalette(pal);
        update();
    }
}


void UBFloatingPalette::setCustomPosition(bool pFlag)
{
    mCustomPosition = pFlag;

    if (pFlag)
        removeAllAssociatedPalette();

}

int UBFloatingPalette::radius()
{
    return UBGlassStyle::panelRadius;
}


int UBFloatingPalette::border()
{
    return 0;
}

int UBFloatingPalette::closeButtonMargin() const
{
    // inside the rounded glass body, clear of its corner
    return const_cast<UBFloatingPalette*>(this)->border() + 10;
}

int UBFloatingPalette::closeButtonTop() const
{
    // close button (22 px) plus a small gap
    return closeButtonMargin() + 22 + 4;
}

void UBFloatingPalette::onCloseButtonClicked()
{
    hide();
}

void UBFloatingPalette::enterEvent(UB::EnterEvent *event)
{
    Q_UNUSED(event);
    emit mouseEntered();
}

void UBFloatingPalette::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        mIsMoving = true;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        mDragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        mDragPosition = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
    }
    else
    {
        QWidget::mousePressEvent(event);
    }
}

void UBFloatingPalette::mouseMoveEvent(QMouseEvent *event)
{
    if (mIsMoving)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        moveInsideParent(event->globalPosition().toPoint() - mDragPosition);
#else
        moveInsideParent(event->globalPos() - mDragPosition);
#endif
        event->accept();
        emit moving();
    }
    else
    {
        QWidget::mouseMoveEvent(event);
    }
}

void UBFloatingPalette::mouseReleaseEvent(QMouseEvent *event)
{
    if (mIsMoving)
    {
        mIsMoving = false;
        setCustomPosition(true);
        event->accept();
    }
    else
    {
        QWidget::mouseReleaseEvent(event);
    }
}

int UBFloatingPalette::getParentRightOffset()
{
    return 0;
}

void UBFloatingPalette::moveInsideParent(const QPoint &position)
{
    QWidget *parent = parentWidget();

    if (parent)
    {
        int margin = UBSettings::boardMargin - border();
        qreal newX = qMax(margin, qMin(parent->width() - getParentRightOffset() - width() - margin, position.x()));
        qreal newY = qMax(margin, qMin(parent->height() - height() - margin, position.y()));

        if (!mCustomPosition && !mIsMoving)
        {
            if (mDefaultPosition == Qt::TopLeftCorner || mDefaultPosition == Qt::BottomLeftCorner)
            {
                newX = margin;
            }
            else
            {
                newX = qMax(margin, parent->width() - getParentRightOffset() - width() - margin);
            }
        }
        move(newX, newY);
        minimizePalette(QPoint(newX, newY));
    }
    else
    {
        move(position);
    }
}

void UBFloatingPalette::addAssociatedPalette(UBFloatingPalette* pOtherPalette)
{
    mAssociatedPalette.append(pOtherPalette);
}

void UBFloatingPalette::removeAssociatedPalette(UBFloatingPalette* pOtherPalette)
{
    mAssociatedPalette.removeOne(pOtherPalette);
}

QSize UBFloatingPalette::preferredSize()
{
    QSize palettePreferredSize = sizeHint();

    if (palettePreferredSize.width() == 0)
    {
        palettePreferredSize = childrenRect().size();
    }

    return palettePreferredSize;
}

void UBFloatingPalette::adjustSizeAndPosition(bool pUp)
{
    QSize newPreferredSize = preferredSize();

    foreach (UBFloatingPalette* palette, mAssociatedPalette)
    {
        QSize palettePreferredSize = palette->preferredSize();
        newPreferredSize.setWidth(newPreferredSize.expandedTo(palettePreferredSize).width());
    }
    QSize previousSize = size();
    int biggerHeight = preferredSize().height() - previousSize.height();
    if ((pUp && (biggerHeight > 0))
        || (!pUp && (biggerHeight < 0)))
    {
        move(pos().x(), pos().y() - biggerHeight);
    }

    if (newPreferredSize != size())
    {
        resize(newPreferredSize);
        moveInsideParent(pos());
        foreach(UBFloatingPalette* palette, mAssociatedPalette)
        {
            palette->move(pos().x(), palette->pos().y());
            palette->resize(newPreferredSize.width(), palette->size().height());
        }
    }
}

void UBFloatingPalette::removeAllAssociatedPalette()
{
    foreach (UBFloatingPalette* palette, mAssociatedPalette)
    {
        palette->removeAssociatedPalette(this);
        removeAssociatedPalette(palette);
    }
}

void UBFloatingPalette::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintGlassBackground(painter);
}


QRectF UBFloatingPalette::glassRect()
{
    // the border area around the body holds the drop shadow
    const int b = qMax(0, border());
    return QRectF(rect()).adjusted(b, b, -b, -b);
}


void UBFloatingPalette::paintGlassBackground(QPainter& painter)
{
    if (UBUiStyle::isModern())
    {
        paintModernBackground(painter);
        return;
    }

    // a native glass view of the platform is behind the panel: controls only
    if (hasNativeGlass())
        return;

    const QRectF body = glassRect();
    const qreal r = radius();
    const qreal dpr = devicePixelRatio();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    if (mShadow.isNull() || mShadow.size() != size() * dpr || mShadowOnDark != mOnDark)
    {
        mShadow = UBGlassStyle::shadowImage(size(), body, r, dpr, mOnDark);
        mShadowOnDark = mOnDark;
    }

    painter.drawImage(QPointF(0, 0), mShadow);

    const bool active = backdropActive();

    if (active && (!mBackdrop.isValid() || mBackdrop.rect.size() != QSizeF(size())))
        scheduleBackdropUpdate();

    // the widget is not opaque: without backdrop the content behind shows through the tint
    UBGlassStyle::paintGlass(&painter, body, r, active ? mBackdrop : UBGlassBackdrop(), mOnDark);

    painter.restore();
}


void UBFloatingPalette::paintModernBackground(QPainter& painter)
{
    // a solid card in the colours of the theme: no backdrop, the corners are always 8 px
    const QRectF body = glassRect();
    const qreal r = UBModernStyle::panelRadius;
    const qreal dpr = devicePixelRatio();

    if (mShadow.isNull() || mShadow.size() != size() * dpr || mShadowOnDark != mOnDark)
    {
        mShadow = UBModernStyle::shadowImage(size(), body, r, dpr, mOnDark);
        mShadowOnDark = mOnDark;
    }

    painter.drawImage(QPointF(0, 0), mShadow);
    UBModernStyle::paintPanel(&painter, body, r, mOnDark);
}


void UBFloatingPalette::setGlassBackdropEnabled(bool enabled)
{
    mGlassBackdropEnabled = enabled;

    if (!enabled)
        mBackdrop = UBGlassBackdrop();

    updateMaterial();
    scheduleBackdropUpdate();
    update();
}


bool UBFloatingPalette::hasNativeGlass() const
{
    return property("ubNativeGlass").toBool();
}


bool UBFloatingPalette::backdropActive() const
{
    // also with native glass: the luminance of the content below selects the light or dark material
    // (glass appearance and glyphs), like the system glass does for small controls
    // the modern style has no backdrop: the material follows the theme
    return UBUiStyle::isGlass() && mGlassBackdropEnabled && parentWidget() && !isWindow()
            && (!hasNativeGlass() || !qEnvironmentVariableIsSet("OPENBOARD_GLASS_ADAPTIVE")
                || qEnvironmentVariableIntValue("OPENBOARD_GLASS_ADAPTIVE") != 0);
}


void UBFloatingPalette::scheduleBackdropUpdate()
{
    if (!backdropActive() || !isVisible())
        return;

    if (!mBackdropTimer->isActive())
        mBackdropTimer->start();
}


void UBFloatingPalette::updateMaterial()
{
    bool onDark = UBGlassStyle::isDark();

    // the material adapts to the content below the glass, the theme decides without content
    if (backdropActive() && mBackdrop.isValid() && mBackdrop.coverage > 0.5)
    {
        const bool wasDark = property("glassOnDark").toBool();
        onDark = wasDark ? mBackdrop.luminance < lightAbove : mBackdrop.luminance < darkBelow;
    }

    if (onDark != mOnDark || property("glassOnDark").toBool() != onDark)
    {
        mOnDark = onDark;
        UBGlassStyle::setPanelMaterial(this, onDark);
        update();
    }
}


void UBFloatingPalette::publishGlassGeometry()
{
    // used by the native glass of the platform (UBGlassEffect)
    const QRectF body = glassRect();
    setProperty("ubGlassBody", body.toRect());
    setProperty("ubGlassRadius", qMin<qreal>(radius(), qMin(body.width(), body.height()) / 2.0));
}


void UBFloatingPalette::updateBackdrop()
{
    mBackdropTimer->stop();

    if (!backdropActive() || !isVisible() || sRenderingBackdrop > 0 || size().isEmpty())
    {
        if (!backdropActive())
        {
            mBackdrop = UBGlassBackdrop();
            updateMaterial();
        }
        return;
    }

    QWidget* parent = parentWidget();
    const QRect geometryInParent = geometry();
    const qreal dpr = devicePixelRatio();

    // translucent windows (desktop annotation) show the desktop behind, which cannot
    // be captured: only the content of the window is used, the rest stays transparent
    const bool translucentWindow = window()->testAttribute(Qt::WA_TranslucentBackground);

    QImage raw(size() * dpr, QImage::Format_ARGB32_Premultiplied);
    raw.setDevicePixelRatio(dpr);
    raw.fill(translucentWindow ? QColor(Qt::transparent) : parent->palette().color(QPalette::Window));

    int rendered = 0;

    ++sRenderingBackdrop;
    {
        QPainter painter(&raw);

        // render the siblings below this palette, never the palette itself or what is above it
        for (QObject* child : parent->children())
        {
            if (child == this)
                break;

            QWidget* sibling = qobject_cast<QWidget*>(child);

            if (!sibling || sibling->isWindow() || !sibling->isVisible())
                continue;

            const QRect overlap = sibling->geometry() & geometryInParent;

            if (overlap.isEmpty())
                continue;

            // other glass panels only paint their body and shadow, not the window background
            const QWidget::RenderFlags flags = qobject_cast<UBFloatingPalette*>(sibling)
                    ? QWidget::RenderFlags(QWidget::DrawChildren)
                    : (QWidget::DrawWindowBackground | QWidget::DrawChildren);

            const QRect source = overlap.translated(-sibling->pos());
            sibling->render(&painter, overlap.topLeft() - geometryInParent.topLeft(), QRegion(source), flags);
            ++rendered;
        }
    }
    --sRenderingBackdrop;

    static bool reported = false;

    if (rendered == 0 && !translucentWindow && !reported)
    {
        reported = true;
        qDebug() << "UBFloatingPalette: no content captured behind" << objectName() << "- glass without backdrop";
    }

    UBGlassBackdrop backdrop;
    backdrop.rect = QRectF(rect());
    backdrop.blurred = UBGlassStyle::blurredBackdrop(raw, qRound(backdropBlurRadius * dpr), 4, 1.2);
    backdrop.lens = UBGlassStyle::blurredBackdrop(raw, qRound(lensBlurRadius * dpr), 2, 1.1);

    // luminance of the content under the body only
    const QRectF body = glassRect();
    const qreal sx = backdrop.blurred.width() / qMax<qreal>(1.0, width());
    const qreal sy = backdrop.blurred.height() / qMax<qreal>(1.0, height());
    const QRect bodyInSmall = QRectF(body.left() * sx, body.top() * sy, body.width() * sx, body.height() * sy).toAlignedRect()
            & backdrop.blurred.rect();
    backdrop.luminance = UBGlassStyle::meanLuminance(backdrop.blurred.copy(bodyInSmall), &backdrop.coverage);

    mBackdrop = backdrop;
    mBackdropAge.restart();

    updateMaterial();
    update();
}


bool UBFloatingPalette::event(QEvent *event)
{
    switch (event->type())
    {
        case QEvent::ParentChange:
            watchSiblings();
            mBackdrop = UBGlassBackdrop();
            break;

        case QEvent::Show:
            publishGlassGeometry();
            if (UBUiStyle::isGlass())
                UBGlassEffect::applyToPanel(this);
            // capture the backdrop before the first paint
            updateBackdrop();
            break;

        case QEvent::Resize:
            mShadow = QImage();
            publishGlassGeometry();
            if (isVisible())
                updateBackdrop();
            break;

        case QEvent::Move:
            if (isVisible())
            {
                if (mIsMoving && (!mBackdropAge.isValid() || mBackdropAge.elapsed() >= backdropDragMs))
                    updateBackdrop();
                else
                    scheduleBackdropUpdate();
            }
            break;

        default:
            break;
    }

    return QWidget::event(event);
}


void UBFloatingPalette::watchSiblings()
{
    for (const QPointer<QWidget>& watched : std::as_const(mWatched))
    {
        if (watched)
            watched->removeEventFilter(mWatcher);
    }

    mWatched.clear();

    QWidget* parent = parentWidget();

    if (!parent)
        return;

    auto watch = [this](QWidget* widget) {
        widget->installEventFilter(mWatcher);
        mWatched << widget;
    };

    // the parent tells about new siblings
    watch(parent);

    for (QObject* child : parent->children())
    {
        QWidget* sibling = qobject_cast<QWidget*>(child);

        if (!sibling || sibling == this)
            continue;

        watch(sibling);

        // scroll areas (the board view) paint in their viewport
        if (QAbstractScrollArea* scrollArea = qobject_cast<QAbstractScrollArea*>(sibling))
        {
            if (scrollArea->viewport())
                watch(scrollArea->viewport());
        }
    }
}


void UBFloatingPalette::siblingEvent(QWidget* sibling, QEvent* event)
{
    if (sRenderingBackdrop > 0 || !isVisible() || !backdropActive())
        return;

    QWidget* parent = parentWidget();

    if (!parent)
        return;

    if (sibling != parent)
    {
        // ignore widgets stacked above this palette, they are not part of the backdrop
        QWidget* topSibling = sibling;

        while (topSibling && topSibling->parentWidget() != parent)
            topSibling = topSibling->parentWidget();

        const QObjectList& children = parent->children();

        if (!topSibling || children.indexOf(topSibling) > children.indexOf(this))
            return;
    }

    if (sibling == parent)
    {
        if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved)
        {
            // re-install the filters later, the new child is not fully constructed yet
            QTimer::singleShot(0, this, [this]() {
                watchSiblings();
                scheduleBackdropUpdate();
            });
        }
        else if (event->type() == QEvent::Resize)
        {
            scheduleBackdropUpdate();
        }

        return;
    }

    switch (event->type())
    {
        case QEvent::Paint:
        {
            // only repaints of the area covered by this palette matter
            const QRect dirty = static_cast<QPaintEvent*>(event)->region().boundingRect();
            const QRect dirtyInParent(sibling->mapTo(parent, dirty.topLeft()), dirty.size());

            if (dirtyInParent.intersects(geometry()))
                scheduleBackdropUpdate();
            break;
        }

        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ZOrderChange:
            scheduleBackdropUpdate();
            break;

        default:
            break;
    }
}


int UBFloatingPalette::gripSize()
{
    return 5;
}

void UBFloatingPalette::buildCloseButton()
{
    if (mCloseButton)
        return;

    mCloseButton = new QToolButton(this);
    mCloseButton->setObjectName("closeButton");
    mCloseButton->setAutoRaise(true);
    mCloseButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mCloseButton->setText(QString());
    mCloseButton->setFixedSize(22, 22);
    mCloseButton->setIconSize(QSize(12, 12));
    mCloseButton->setIcon(UBGlassStyle::icon("close"));
    mCloseButton->setToolTip(tr("Close"));
    const int margin = closeButtonMargin();
    mCloseButton->move(margin, margin);
    mCloseButton->hide();

    connect(mCloseButton, &QToolButton::clicked, this, [this]() {
        onCloseButtonClicked();
    });
}

void UBFloatingPalette::minimizePalette(const QPoint& pos)
{
    if(!mCanBeMinimized)
    {
        //  If this floating palette cannot be minimized, we exit this method.
    return;
    }

    if(mMinimizedLocation == eMinimizedLocation_None)
    {
    //  Verify if we have to minimize this palette
    if(pos.x() == 5)
    {
        mMinimizedLocation = eMinimizedLocation_Left;
    }
//    else if(pos.y() == 5)
//    {
//        mMinimizedLocation = eMinimizedLocation_Top;
//    }
    else if(pos.x() == parentWidget()->width() - getParentRightOffset() - width() - 5)
    {
        mMinimizedLocation = eMinimizedLocation_Right;
    }
//    else if(pos.y() == parentSize.height() - height() - 5)
//    {
//        mMinimizedLocation = eMinimizedLocation_Bottom;
//    }

    //  Minimize the Palette
    if(mMinimizedLocation != eMinimizedLocation_None)
    {
        emit minimizeStart(mMinimizedLocation);
    }
    }
    else
    {
    //  Restore the palette
    if(pos.x() > 5 &&
       pos.y() > 5 &&
       pos.x() < parentWidget()->width() - getParentRightOffset()  - width() - 5 &&
       pos.y() < parentWidget()->size().height() - height() - 5)
    {
        mMinimizedLocation = eMinimizedLocation_None;
        emit maximizeStart();
    }
    }
}

void UBFloatingPalette::setMinimizePermission(bool permission)
{
    mCanBeMinimized = permission;
}
