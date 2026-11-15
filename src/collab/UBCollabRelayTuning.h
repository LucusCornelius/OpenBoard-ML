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

#ifndef UBCOLLABRELAYTUNING_H_
#define UBCOLLABRELAYTUNING_H_

#include <QtGlobal>

class UBCollabRelayChannel;

/**
 * The times and sizes of UBCollabRelayChannel. The program uses the defaults;
 * the console tests make the times short and cut lines on purpose. Changes
 * apply to channels that start or join after them.
 */
struct UBCollabRelayTuning
{
    int handshakeTimeoutMs = 15000;     // TCP, TLS and the upgrade of one attempt
    int keepAliveMs = 30000;            // a keep-alive goes to the relay this often
    int silenceLimitMs = 50000;         // nothing from the relay for this long: the line is dead
    int reconnectWindowMs = 60000;      // how long a dropped line is tried again
    int firstRetryDelayMs = 250;        // doubles after each failed attempt ...
    int maxRetryDelayMs = 8000;         // ... up to this
    int answerTimeoutMs = 60000;        // joining: time for the first answer of a participant

    qint64 chunkSize = 256 * 1024;      // plaintext per relay message, at most; less when the relay asks
    qint64 writeBuffer = 1024 * 1024;   // no more than this waits in the socket
    qint64 maxMessageSize = qint64(256) * 1024 * 1024;  // per message, both ways
    qint64 maxIncoming = qint64(512) * 1024 * 1024;     // of all partly received messages together

    static UBCollabRelayTuning& current();

    /** for tests: the line of \a channel drops as if the network went away */
    static void dropLine(UBCollabRelayChannel* channel);
};

#endif /* UBCOLLABRELAYTUNING_H_ */
