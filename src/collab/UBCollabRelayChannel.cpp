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

#include "UBCollabRelayChannel.h"

// Placeholder: the relay connection is not written yet. Every session ends at
// once with RelayUnreachable, so the code that uses the channel can be built.

class UBCollabRelayChannelPrivate
{
    public:
        UBCollabChannel::Error error{UBCollabChannel::NoError};
};

UBCollabRelayChannel::UBCollabRelayChannel(QObject* parent)
    : UBCollabChannel(parent)
    , d(new UBCollabRelayChannelPrivate)
{
}

UBCollabRelayChannel::~UBCollabRelayChannel()
{
    delete d;
}

QString UBCollabRelayChannel::newSessionCode()
{
    return QString();
}

QString UBCollabRelayChannel::normalizedCode(const QString& text)
{
    return text.trimmed();
}

QString UBCollabRelayChannel::invitation(const QUrl& relay, const QString& code)
{
    return relay.toString() + QLatin1Char('#') + code;
}

bool UBCollabRelayChannel::parseInvitation(const QString& text, QUrl& relay, QString& code)
{
    Q_UNUSED(text);
    Q_UNUSED(relay);
    Q_UNUSED(code);
    return false;
}

bool UBCollabRelayChannel::isRelayAddress(const QUrl& relay)
{
    return relay.isValid() && !relay.host().isEmpty();
}

void UBCollabRelayChannel::start(const QUrl& relay, const QString& code, const QString& relayKey)
{
    Q_UNUSED(relay);
    Q_UNUSED(code);
    Q_UNUSED(relayKey);
    d->error = RelayUnreachable;
    emit stateChanged();
}

void UBCollabRelayChannel::join(const QUrl& relay, const QString& code)
{
    Q_UNUSED(relay);
    Q_UNUSED(code);
    d->error = RelayUnreachable;
    emit stateChanged();
}

UBCollabChannel::State UBCollabRelayChannel::state() const
{
    return Closed;
}

UBCollabChannel::Error UBCollabRelayChannel::error() const
{
    return d->error;
}

QString UBCollabRelayChannel::errorString() const
{
    return QString();
}

UBCollabChannel::PeerId UBCollabRelayChannel::localPeer() const
{
    return 0;
}

QList<UBCollabChannel::PeerId> UBCollabRelayChannel::peers() const
{
    return {};
}

void UBCollabRelayChannel::send(const QByteArray& message, PeerId to)
{
    Q_UNUSED(message);
    Q_UNUSED(to);
}

qint64 UBCollabRelayChannel::bytesToSend() const
{
    return 0;
}

void UBCollabRelayChannel::close()
{
}
