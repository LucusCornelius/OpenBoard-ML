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

#include "UBCollabWire.h"

#include <QtEndian>

#include <cmath>
#include <cstring>

#include "core/memcheck.h"

void UBCollabWriter::u8(quint8 value)
{
    mData.append(static_cast<char>(value));
}


void UBCollabWriter::u32(quint32 value)
{
    char buffer[4];
    qToLittleEndian<quint32>(value, buffer);
    mData.append(buffer, 4);
}


void UBCollabWriter::i32(qint32 value)
{
    u32(static_cast<quint32>(value));
}


void UBCollabWriter::u64(quint64 value)
{
    char buffer[8];
    qToLittleEndian<quint64>(value, buffer);
    mData.append(buffer, 8);
}


void UBCollabWriter::f64(double value)
{
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double must have 64 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
}


void UBCollabWriter::boolean(bool value)
{
    u8(value ? 1 : 0);
}


void UBCollabWriter::bytes(const QByteArray& value)
{
    u32(static_cast<quint32>(value.size()));
    mData.append(value);
}


void UBCollabWriter::string(const QString& value)
{
    bytes(value.toUtf8());
}


void UBCollabWriter::uuid(const QUuid& value)
{
    mData.append(value.toRfc4122());
}


void UBCollabWriter::version(const UBCollabVersion& value)
{
    u64(value.counter);
    u32(value.peer);
}


UBCollabReader::UBCollabReader(const QByteArray& data)
    : mData(data)
{
}


bool UBCollabReader::take(qsizetype length)
{
    if (!mOk)
        return false;

    if (length < 0 || length > remaining())
    {
        fail(QString("truncated message at byte %1").arg(mPos));
        return false;
    }

    return true;
}


void UBCollabReader::fail(const QString& reason)
{
    if (mOk)
    {
        mOk = false;
        mError = reason;
    }
}


quint8 UBCollabReader::u8()
{
    if (!take(1))
        return 0;

    return static_cast<quint8>(mData.at(mPos++));
}


quint32 UBCollabReader::u32()
{
    if (!take(4))
        return 0;

    const quint32 value = qFromLittleEndian<quint32>(mData.constData() + mPos);
    mPos += 4;
    return value;
}


qint32 UBCollabReader::i32()
{
    return static_cast<qint32>(u32());
}


quint64 UBCollabReader::u64()
{
    if (!take(8))
        return 0;

    const quint64 value = qFromLittleEndian<quint64>(mData.constData() + mPos);
    mPos += 8;
    return value;
}


double UBCollabReader::f64()
{
    const quint64 bits = u64();
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));

    if (!std::isfinite(value))
    {
        fail("number that is not finite");
        return 0;
    }

    return mOk ? value : 0;
}


bool UBCollabReader::boolean()
{
    const quint8 value = u8();

    if (value > 1)
    {
        fail("invalid boolean");
        return false;
    }

    return value == 1;
}


QByteArray UBCollabReader::bytes(qsizetype maxLength)
{
    const quint32 length = u32();

    if (!mOk)
        return {};

    if (qsizetype(length) > maxLength)
    {
        fail(QString("field of %1 bytes, at most %2 allowed").arg(length).arg(maxLength));
        return {};
    }

    if (!take(length))
        return {};

    const QByteArray value = mData.mid(mPos, length);
    mPos += length;
    return value;
}


QString UBCollabReader::string(qsizetype maxBytes)
{
    return QString::fromUtf8(bytes(maxBytes));
}


QUuid UBCollabReader::optionalUuid()
{
    if (!take(16))
        return {};

    const QUuid value = QUuid::fromRfc4122(mData.mid(mPos, 16));
    mPos += 16;
    return value;
}


QUuid UBCollabReader::uuid()
{
    const QUuid value = optionalUuid();

    if (mOk && value.isNull())
        fail("null uuid");

    return mOk ? value : QUuid();
}


UBCollabVersion UBCollabReader::version()
{
    UBCollabVersion value;
    value.counter = u64();
    value.peer = u32();

    // a clock this far cannot be reached by counting; it would leave no room for later changes
    if (value.counter > (Q_UINT64_C(1) << 60))
        fail("clock value out of range");

    return mOk ? value : UBCollabVersion();
}


quint32 UBCollabReader::count(quint32 max, qsizetype minItemBytes)
{
    const quint32 value = u32();

    if (!mOk)
        return 0;

    if (value > max)
    {
        fail(QString("%1 entries, at most %2 allowed").arg(value).arg(max));
        return 0;
    }

    if (minItemBytes > 0 && qsizetype(value) > remaining() / minItemBytes)
    {
        fail(QString("%1 entries cannot fit in the message").arg(value));
        return 0;
    }

    return value;
}
