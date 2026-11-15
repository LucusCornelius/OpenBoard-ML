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

#include "UBCollabPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "gui/UBGlassStyle.h"
#include "gui/UBModernStyle.h"
#include "gui/UBUiStyle.h"

#include "UBCollabController.h"
#include "UBCollabSession.h"

#include "core/memcheck.h"

namespace
{
    constexpr int panelWidth = 300;

    QPixmap dot(const QColor& color, qreal dpr)
    {
        const int size = 12;
        QPixmap pixmap(QSize(size, size) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
        return pixmap;
    }

    QColor accentColor(bool onDark)
    {
        return UBUiStyle::isModern() ? UBModernStyle::accent(UBGlassStyle::isDark()) : (onDark ? UBGlassStyle::accent().lighter(115) : UBGlassStyle::accent());
    }

    /** a flat hairline between the sections, visible on light and dark glass */
    QFrame* separator(QWidget* parent)
    {
        QFrame* line = new QFrame(parent);
        line->setObjectName("collabSeparator");
        line->setFixedHeight(1);
        line->setFrameShape(QFrame::NoFrame);
        line->setStyleSheet("QFrame#collabSeparator { background: rgba(128, 128, 128, 70); border: none; }");
        return line;
    }

    QLabel* sectionTitle(const QString& text, QWidget* parent)
    {
        QLabel* label = new QLabel(text, parent);
        QFont font = label->font();
        font.setWeight(QFont::DemiBold);
        label->setFont(font);
        return label;
    }
}


UBCollabProgressBar::UBCollabProgressBar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedHeight(6);
}


void UBCollabProgressBar::setValue(qreal value)
{
    mValue = value;
    update();
}


QSize UBCollabProgressBar::sizeHint() const
{
    return QSize(200, 6);
}


void UBCollabProgressBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    UBFloatingPalette* palette = qobject_cast<UBFloatingPalette*>(window());
    const bool onDark = palette ? palette->isOnDarkMaterial() : UBGlassStyle::isDark();

    // a flat capsule track with a flat accent fill
    const QRectF track = QRectF(rect()).adjusted(0, 1, 0, -1);
    const qreal radius = track.height() / 2;
    painter.setBrush(onDark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 30));
    painter.drawRoundedRect(track, radius, radius);

    const qreal value = mValue < 0 ? 0.25 : qBound(0.0, mValue, 1.0);
    QRectF fill = track;
    fill.setWidth(qMax(track.height(), track.width() * value));
    painter.setBrush(accentColor(onDark));
    painter.drawRoundedRect(fill, radius, radius);
}


UBCollabPanel::UBCollabPanel(UBCollabController* controller, QWidget* parent)
    : UBFloatingPalette(Qt::TopRightCorner, parent)
    , mController(controller)
{
    setObjectName("collabPanel");

    QVBoxLayout* layout = new QVBoxLayout(this);
    const int b = border();
    layout->setContentsMargins(b + 16, b + 14, b + 16, b + 16);
    layout->setSpacing(10);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    mTitle = new QLabel(tr("Work together"), this);
    QFont titleFont = mTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1);
    mTitle->setFont(titleFont);
    // room for the close button at the right
    mTitle->setContentsMargins(0, 0, 28, 0);
    layout->addWidget(mTitle);

    mPages = new QStackedWidget(this);
    mPages->setFixedWidth(panelWidth);
    mIdlePage = buildIdlePage();
    mBusyPage = buildBusyPage();
    mSessionPage = buildSessionPage();
    mPages->addWidget(mIdlePage);
    mPages->addWidget(mBusyPage);
    mPages->addWidget(mSessionPage);
    layout->addWidget(mPages);

    mMessage = new QLabel(this);
    mMessage->setObjectName("glassHint");
    mMessage->setWordWrap(true);
    mMessage->setFixedWidth(panelWidth);
    mMessage->hide();
    layout->addWidget(mMessage);

    setClosable(true);
    hide();
}


int UBCollabPanel::border()
{
    return UBGlassStyle::shadowMargin;
}


QToolButton* UBCollabPanel::makeButton(const QString& text, bool primary)
{
    QToolButton* button = new QToolButton(this);
    button->setObjectName("glassButton");
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setText(text);
    button->setMinimumHeight(30);
    button->setMinimumWidth(button->fontMetrics().horizontalAdvance(text) + 28);

    if (primary)
    {
        // the accent fill of a checked glass button, kept checked: the main action of the panel
        button->setCheckable(true);
        button->setChecked(true);
        connect(button, &QToolButton::toggled, button, [button]() { button->setChecked(true); });
    }

    return button;
}


QLabel* UBCollabPanel::makeHint(const QString& text)
{
    QLabel* label = new QLabel(text, this);
    label->setObjectName("glassHint");
    label->setWordWrap(true);
    return label;
}


QWidget* UBCollabPanel::buildIdlePage()
{
    QWidget* page = new QWidget(mPages);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    mStartButton = makeButton(tr("Start session"), true);
    layout->addWidget(mStartButton, 0, Qt::AlignLeft);
    layout->addWidget(makeHint(tr("Shares the document on the board. Pass the invitation on to the others.")));

    layout->addWidget(separator(page));

    layout->addWidget(sectionTitle(tr("Join a session"), page));

    QHBoxLayout* joinRow = new QHBoxLayout();
    joinRow->setSpacing(6);
    mInvitationInput = new QLineEdit(page);
    mInvitationInput->setPlaceholderText(tr("Paste the invitation"));
    mInvitationInput->setClearButtonEnabled(true);
    mJoinButton = makeButton(tr("Join"), false);
    joinRow->addWidget(mInvitationInput, 1);
    joinRow->addWidget(mJoinButton);
    layout->addLayout(joinRow);

    layout->addWidget(separator(page));

    layout->addWidget(sectionTitle(tr("Your name"), page));
    mNameInput = new QLineEdit(page);
    mNameInput->setPlaceholderText(tr("Participant"));
    mNameInput->setMaxLength(40);
    layout->addWidget(mNameInput);

    layout->addWidget(separator(page));
    layout->addWidget(sectionTitle(tr("Relay"), page));
    layout->addWidget(makeHint(tr("Only to start a session: the address of a relay and its key, if it asks one. They come from whoever set the relay up.")));
    mRelayInput = new QLineEdit(page);
    mRelayInput->setPlaceholderText(tr("wss://relay.example.org"));
    layout->addWidget(mRelayInput);
    mRelayKeyInput = new QLineEdit(page);
    mRelayKeyInput->setPlaceholderText(tr("Relay key (optional)"));
    mRelayKeyInput->setEchoMode(QLineEdit::Password);
    layout->addWidget(mRelayKeyInput);

    connect(mStartButton, &QToolButton::clicked, this, &UBCollabPanel::startClicked);
    connect(mJoinButton, &QToolButton::clicked, this, &UBCollabPanel::joinClicked);
    connect(mInvitationInput, &QLineEdit::returnPressed, this, &UBCollabPanel::joinClicked);

    for (QLineEdit* field : {mNameInput, mRelayInput, mRelayKeyInput})
        connect(field, &QLineEdit::editingFinished, this, &UBCollabPanel::saveSettings);

    return page;
}


QWidget* UBCollabPanel::buildBusyPage()
{
    QWidget* page = new QWidget(mPages);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    mBusyLabel = new QLabel(page);
    mBusyLabel->setWordWrap(true);
    layout->addWidget(mBusyLabel);

    mProgress = new UBCollabProgressBar(page);
    layout->addWidget(mProgress);

    mCancelButton = makeButton(tr("Cancel"), false);
    layout->addWidget(mCancelButton, 0, Qt::AlignLeft);

    connect(mCancelButton, &QToolButton::clicked, this, [this]() {
        if (mController)
            mController->leaveSession();
    });

    return page;
}


QWidget* UBCollabPanel::buildSessionPage()
{
    QWidget* page = new QWidget(mPages);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    mSessionLabel = new QLabel(page);
    mSessionLabel->setWordWrap(true);
    layout->addWidget(mSessionLabel);

    mInvitationBox = new QWidget(page);
    QVBoxLayout* invitationLayout = new QVBoxLayout(mInvitationBox);
    invitationLayout->setContentsMargins(0, 0, 0, 0);
    invitationLayout->setSpacing(6);
    invitationLayout->addWidget(makeHint(tr("Pass this invitation on to the people you work with:")));
    mInvitationOutput = new QLineEdit(mInvitationBox);
    mInvitationOutput->setReadOnly(true);
    invitationLayout->addWidget(mInvitationOutput);
    mCopyButton = makeButton(tr("Copy invitation"), true);
    invitationLayout->addWidget(mCopyButton, 0, Qt::AlignLeft);
    layout->addWidget(mInvitationBox);

    layout->addWidget(separator(page));

    mParticipantList = new QWidget(page);
    mParticipantLayout = new QVBoxLayout(mParticipantList);
    mParticipantLayout->setContentsMargins(0, 0, 0, 0);
    mParticipantLayout->setSpacing(4);
    layout->addWidget(mParticipantList);

    layout->addWidget(separator(page));

    mFollowBox = new QCheckBox(tr("Follow page"), page);
    mFollowBox->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(mFollowBox);
    layout->addWidget(makeHint(tr("Go along when someone turns the page. Off: you browse on your own and the others do not follow you.")));

    mLeaveButton = makeButton(tr("Leave session"), false);
    layout->addWidget(mLeaveButton, 0, Qt::AlignLeft);

    connect(mCopyButton, &QToolButton::clicked, this, &UBCollabPanel::copyClicked);
    connect(mFollowBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (mController)
            mController->setFollowsPage(checked);
    });
    connect(mLeaveButton, &QToolButton::clicked, this, [this]() {
        if (mController)
            mController->leaveSession();
    });

    return page;
}


void UBCollabPanel::rebuildParticipants()
{
    while (QLayoutItem* item = mParticipantLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    UBCollabSession* session = mController ? mController->session() : nullptr;

    if (!session)
        return;

    for (const UBCollabSession::Participant& participant : session->participants())
    {
        QWidget* row = new QWidget(mParticipantList);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        QLabel* mark = new QLabel(row);
        mark->setPixmap(dot(participant.color, devicePixelRatioF()));
        rowLayout->addWidget(mark);

        QLabel* name = new QLabel(UBCollabSession::displayName(participant), row);
        rowLayout->addWidget(name, 1);

        QStringList tags;

        if (participant.local)
            tags << tr("you");

        if (participant.host)
            tags << tr("host");

        if (!tags.isEmpty())
            rowLayout->addWidget(makeHint(tags.join(", ")));

        mParticipantLayout->addWidget(row);
    }
}


void UBCollabPanel::refresh()
{
    if (!mController)
        return;

    UBCollabSession* session = mController->session();
    const QString message = mController->lastMessage();

    mMessage->setText(message);
    mMessage->setVisible(!message.isEmpty() && (!session || session->state() == UBCollabSession::Ended));

    if (!session || session->state() == UBCollabSession::Ended)
    {
        mPages->setCurrentWidget(mIdlePage);

        if (!mNameInput->hasFocus())
            mNameInput->setText(UBCollabController::displayName());

        if (!mRelayInput->hasFocus())
            mRelayInput->setText(UBCollabController::relayAddress());

        if (!mRelayKeyInput->hasFocus())
            mRelayKeyInput->setText(UBCollabController::relayKey());
    }
    else if (session->state() != UBCollabSession::Active)
    {
        mPages->setCurrentWidget(mBusyPage);

        const qint64 total = session->snapshotTotal();

        if (session->state() == UBCollabSession::Connecting)
        {
            mBusyLabel->setText(session->role() == UBCollabSession::Host ? tr("Starting the session...") : tr("Connecting..."));
            mProgress->setValue(-1);
        }
        else if (total > 0)
        {
            const int percent = int(std::clamp<qint64>(session->snapshotReceived() * 100 / total, 0, 100));
            mBusyLabel->setText(tr("Receiving the board... %1%").arg(percent));
            mProgress->setValue(qreal(session->snapshotReceived()) / qreal(total));
        }
        else
        {
            mBusyLabel->setText(tr("Waiting for the board of the host..."));
            mProgress->setValue(-1);
        }
    }
    else
    {
        mPages->setCurrentWidget(mSessionPage);

        const int count = session->participantCount();
        const QString people = count == 1 ? tr("only you so far") : tr("%1 people").arg(count);
        mSessionLabel->setText(session->role() == UBCollabSession::Host
                               ? tr("Your session is on: %1.").arg(people)
                               : tr("You are in a session: %1.").arg(people));

        const bool host = session->role() == UBCollabSession::Host && !mController->invitation().isEmpty();
        mInvitationBox->setVisible(host);
        mInvitationOutput->setText(mController->invitation());
        mInvitationOutput->setCursorPosition(0);

        mFollowBox->blockSignals(true);
        mFollowBox->setChecked(UBCollabController::followsPage());
        mFollowBox->blockSignals(false);

        mLeaveButton->setText(session->role() == UBCollabSession::Host ? tr("End session") : tr("Leave session"));
        mLeaveButton->setMinimumWidth(mLeaveButton->fontMetrics().horizontalAdvance(mLeaveButton->text()) + 28);

        rebuildParticipants();
    }

    // every page at its own height
    for (QWidget* page : {mIdlePage, mBusyPage, mSessionPage})
        page->setSizePolicy(QSizePolicy::Preferred, page == mPages->currentWidget() ? QSizePolicy::Preferred : QSizePolicy::Ignored);

    mPages->setFixedHeight(mPages->currentWidget()->sizeHint().height());

    if (layout())
        layout()->activate();

    adjustSize();
    reposition();
}


void UBCollabPanel::setAnchor(QWidget* anchor)
{
    if (mAnchor)
        mAnchor->removeEventFilter(this);

    mAnchor = anchor;

    if (mAnchor)
    {
        mAnchor->installEventFilter(this);

        if (mAnchor->window())
            mAnchor->window()->installEventFilter(this);
    }

    if (parentWidget())
        parentWidget()->installEventFilter(this);
}


void UBCollabPanel::togglePanel()
{
    if (isVisible())
    {
        hide();
        return;
    }

    refresh();
    show();
    raise();
}


void UBCollabPanel::reposition()
{
    QWidget* container = parentWidget();

    if (!container)
        return;

    const int b = border();
    const int gap = 8;
    int x = container->width() - width() + b - gap;
    int y = gap - b + 56;

    if (mAnchor && container->isAncestorOf(mAnchor))
    {
        // below the cluster of the button, its right edge at the right of the button
        const QRect anchor(mAnchor->mapTo(container, QPoint(0, 0)), mAnchor->size());
        QWidget* cluster = mAnchor->parentWidget();
        int bottom = anchor.bottom();

        if (UBFloatingPalette* palette = qobject_cast<UBFloatingPalette*>(cluster))
        {
            const int pb = palette->border();
            bottom = palette->geometry().adjusted(pb, pb, -pb, -pb).bottom();
        }

        x = anchor.right() + b - width() + 12;
        y = bottom + gap - b + 2;
    }

    x = qBound(gap - b, x, qMax(gap - b, container->width() - width() + b - gap));
    y = qBound(-b, y, qMax(-b, container->height() - height() + b));

    if (QPoint(x, y) != pos())
        move(x, y);
}


bool UBCollabPanel::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == mAnchor || watched == parentWidget() || (mAnchor && watched == mAnchor->window()))
            && (event->type() == QEvent::Move || event->type() == QEvent::Resize)
            && isVisible() && !mRepositionPending)
    {
        mRepositionPending = true;
        QMetaObject::invokeMethod(this, [this]() {
            mRepositionPending = false;
            reposition();
        }, Qt::QueuedConnection);
    }

    return UBFloatingPalette::eventFilter(watched, event);
}


void UBCollabPanel::showEvent(QShowEvent* event)
{
    UBFloatingPalette::showEvent(event);

    if (mController && mController->action())
        mController->action()->setChecked(true);
}


void UBCollabPanel::hideEvent(QHideEvent* event)
{
    saveSettings();
    UBFloatingPalette::hideEvent(event);

    if (mController && mController->action())
        mController->action()->setChecked(false);
}


void UBCollabPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        hide();
        return;
    }

    UBFloatingPalette::keyPressEvent(event);
}


void UBCollabPanel::mousePressEvent(QMouseEvent* event)
{
    // anchored to its button: not dragged like the other palettes
    event->accept();
}


void UBCollabPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}


void UBCollabPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}


void UBCollabPanel::onCloseButtonClicked()
{
    hide();
}


void UBCollabPanel::saveSettings()
{
    if (!mNameInput)
        return;

    UBCollabController::setDisplayName(mNameInput->text());
    UBCollabController::setRelayAddress(mRelayInput->text());
    UBCollabController::setRelayKey(mRelayKeyInput->text());
}


void UBCollabPanel::startClicked()
{
    saveSettings();

    if (mController)
        mController->startSession();
}


void UBCollabPanel::joinClicked()
{
    saveSettings();

    if (mController && mController->joinSession(mInvitationInput->text()))
        mInvitationInput->clear();
}


void UBCollabPanel::copyClicked()
{
    // only on a click of the user
    if (mController && !mController->invitation().isEmpty())
    {
        QApplication::clipboard()->setText(mController->invitation());
        mCopyButton->setText(tr("Copied"));

        QPointer<QToolButton> button = mCopyButton;
        QTimer::singleShot(1500, this, [button]() {
            if (button)
                button->setText(tr("Copy invitation"));
        });
    }
}
