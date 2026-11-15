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

#ifndef UBCOLLABCHANNEL_H_
#define UBCOLLABCHANNEL_H_

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

/**
 * The line between the people who work on one board: everything the board
 * sync sends and receives goes through this interface, which knows nothing
 * about boards. The implementation for the internet is UBCollabRelayChannel;
 * a test can connect two ends inside one process.
 *
 * Messages are opaque byte arrays of any size. They arrive complete, once,
 * and in the order in which their sender sent them; there is no order between
 * the messages of different senders. A message that was sent is never
 * delivered to its sender.
 */
class UBCollabChannel : public QObject
{
    Q_OBJECT

    public:
        /** identifies a participant during a session; 0 is never a participant */
        typedef quint32 PeerId;

        enum State
        {
            Closed,         // not in a session
            Connecting,     // on the way in, or getting the line back after it dropped
            Open            // in the session: messages can be sent
        };

        enum Error
        {
            NoError,
            RelayUnreachable,   // no connection with the relay could be made
            RelayRefused,       // the relay does not let this participant start a session (key)
            SessionNotFound,    // nobody is in a session with this code
            SessionFull,
            WrongCode,          // the messages of the others cannot be read with this code
            ConnectionLost,     // the line dropped and did not come back
            ProtocolError       // the other side does not speak this protocol (version)
        };

        explicit UBCollabChannel(QObject* parent = nullptr) : QObject(parent) {}

        virtual State state() const = 0;
        virtual Error error() const = 0;

        /** readable explanation of error(), for a log; the interface shows its own translated text */
        virtual QString errorString() const = 0;

        /** this participant; valid while the state is Open */
        virtual PeerId localPeer() const = 0;

        /** the other participants that are in the session now */
        virtual QList<PeerId> peers() const = 0;

        /**
         * Sends a message to one participant, or to all others when \a to is 0.
         * Only while the state is Open; otherwise the message is dropped.
         */
        virtual void send(const QByteArray& message, PeerId to = 0) = 0;

        /** bytes accepted by send() that have not left this computer yet, to show progress and to hold back */
        virtual qint64 bytesToSend() const = 0;

        /** leaves the session; the state becomes Closed without an error */
        virtual void close() = 0;

    signals:
        /** the state changed; when it becomes Closed, error() tells why */
        void stateChanged();

        /**
         * The line dropped and came back while the state stayed in the session.
         * Messages may have been missed in between, in both directions.
         */
        void resumed();

        void peerJoined(UBCollabChannel::PeerId peer);
        void peerLeft(UBCollabChannel::PeerId peer);
        void messageReceived(UBCollabChannel::PeerId from, const QByteArray& message);
};

#endif /* UBCOLLABCHANNEL_H_ */
