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

#include "UBCollabProtocol.h"

#include <QList>

#include "UBCollabWire.h"

#include "core/memcheck.h"

namespace
{
    const char keyDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    constexpr int keyBase = 36;

    int digitValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c >= 'a' && c <= 'z')
            return c - 'a' + 10;

        return -1;
    }
}


void UBCollabProtocol::writeHeader(UBCollabWriter& writer, MessageType type)
{
    writer.u32(magic);
    writer.u8(type);
}


quint8 UBCollabProtocol::readHeader(UBCollabReader& reader)
{
    if (reader.u32() != magic)
    {
        reader.fail("not a message of this protocol");
        return 0;
    }

    const quint8 type = reader.u8();

    if (!reader.ok() || type < Hello || type > LastMessageType)
    {
        reader.fail(QString("unknown message type %1").arg(type));
        return 0;
    }

    return type;
}


QByteArray UBCollabOrderKey::between(const QByteArray& a, const QByteArray& b)
{
    // keys are compared byte by byte; a key never ends in the lowest digit, so that
    // there is always room before it
    if (!b.isEmpty() && a >= b)
    {
        // not ordered (cannot happen with valid keys): stay after a, deterministically
        return a + keyDigits[keyBase / 2];
    }

    QByteArray result;
    bool openEnd = b.isEmpty();

    for (int i = 0; ; ++i)
    {
        const int da = i < a.size() ? qMax(0, digitValue(a.at(i))) : 0;
        const int db = openEnd ? keyBase : (i < b.size() ? qMax(0, digitValue(b.at(i))) : 0);

        if (da == db)
        {
            result.append(keyDigits[da]);
            continue;
        }

        const int mid = (da + db) / 2;

        if (mid > da)
        {
            result.append(keyDigits[mid]);
            return result;
        }

        // db is the next digit after da: keep the digit of a, b no longer limits the rest
        result.append(keyDigits[da]);
        openEnd = true;
    }
}


QList<QByteArray> UBCollabOrderKey::sequence(int count)
{
    QList<QByteArray> keys;

    if (count <= 0)
        return keys;

    // keys of equal length, spread over the range, with room between them
    int width = 2;
    quint64 range = keyBase * keyBase;

    while (range < quint64(count + 1) * 64 && width < 8)
    {
        ++width;
        range *= keyBase;
    }

    const quint64 step = range / quint64(count + 1);

    for (int i = 0; i < count; ++i)
    {
        quint64 value = step * quint64(i + 1);
        QByteArray key(width, '0');

        for (int d = width - 1; d >= 0; --d)
        {
            key[d] = keyDigits[value % keyBase];
            value /= keyBase;
        }

        // a key must not end in the lowest digit; a digit appended keeps the order
        if (key.endsWith('0'))
            key.append(keyDigits[keyBase / 2]);

        keys << key;
    }

    return keys;
}


bool UBCollabOrderKey::isValid(const QByteArray& key)
{
    if (key.isEmpty() || key.size() > UBCollabProtocol::maxOrderKeyBytes || key.endsWith('0'))
        return false;

    for (char c : key)
    {
        if (digitValue(c) < 0)
            return false;
    }

    return true;
}
