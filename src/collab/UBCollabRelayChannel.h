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

#ifndef UBCOLLABRELAYCHANNEL_H_
#define UBCOLLABRELAYCHANNEL_H_

#include <QUrl>

#include "UBCollabChannel.h"

class UBCollabRelayChannelPrivate;

/**
 * A session over the internet, through a relay that only passes messages on
 * between the participants of one session. The messages are encrypted with a
 * key made from the session code, which the relay never gets: it sees who is
 * connected and how much is sent, not what.
 *
 * A session is found by its code. The invitation is one line of text that
 * holds the address of the relay and the code, so the person who joins does
 * not have to set up anything.
 */
class UBCollabRelayChannel : public UBCollabChannel
{
    Q_OBJECT

    public:
        explicit UBCollabRelayChannel(QObject* parent = nullptr);
        ~UBCollabRelayChannel() override;

        /** a new random session code, in the form in which it is shown to the user */
        static QString newSessionCode();

        /** the code as typed or pasted by a user in its standard form; empty when it cannot be a code */
        static QString normalizedCode(const QString& text);

        /** the line of text to pass on to the person who is invited */
        static QString invitation(const QUrl& relay, const QString& code);

        /** takes an invitation apart; false when the text is not an invitation */
        static bool parseInvitation(const QString& text, QUrl& relay, QString& code);

        /** true when the address can be the address of a relay (ws, wss, http or https with a host) */
        static bool isRelayAddress(const QUrl& relay);

        /**
         * Starts a new session with this code. \a relayKey is the key that the
         * relay asks from the person who starts a session; empty when it asks none.
         */
        void start(const QUrl& relay, const QString& code, const QString& relayKey = QString());

        /** joins the session with this code */
        void join(const QUrl& relay, const QString& code);

        State state() const override;
        Error error() const override;
        QString errorString() const override;
        PeerId localPeer() const override;
        QList<PeerId> peers() const override;
        void send(const QByteArray& message, PeerId to = 0) override;
        qint64 bytesToSend() const override;
        void close() override;

    private:
        UBCollabRelayChannelPrivate* d;
};

#endif /* UBCOLLABRELAYCHANNEL_H_ */
