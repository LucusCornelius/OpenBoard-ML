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

#ifndef UBCOLLABWIRE_H_
#define UBCOLLABWIRE_H_

#include <QByteArray>
#include <QString>
#include <QUuid>

/**
 * A logical clock value: the counter of a Lamport clock and the participant
 * that made it. Two versions made by different participants are never equal,
 * so that every conflict has one winner on every computer.
 */
struct UBCollabVersion
{
    quint64 counter{0};
    quint32 peer{0};

    bool isZero() const { return counter == 0 && peer == 0; }

    bool operator==(const UBCollabVersion& other) const { return counter == other.counter && peer == other.peer; }
    bool operator!=(const UBCollabVersion& other) const { return !(*this == other); }

    bool operator<(const UBCollabVersion& other) const
    {
        return counter < other.counter || (counter == other.counter && peer < other.peer);
    }

    bool operator>(const UBCollabVersion& other) const { return other < *this; }
    bool operator<=(const UBCollabVersion& other) const { return !(other < *this); }
    bool operator>=(const UBCollabVersion& other) const { return !(*this < other); }
};


/**
 * Writes the fields of a message in a fixed binary layout (little endian,
 * lengths before variable data), the same on every platform.
 */
class UBCollabWriter
{
    public:
        UBCollabWriter() = default;

        void u8(quint8 value);
        void u32(quint32 value);
        void i32(qint32 value);
        void u64(quint64 value);
        void f64(double value);
        void boolean(bool value);
        void bytes(const QByteArray& value);
        void string(const QString& value);
        void uuid(const QUuid& value);
        void version(const UBCollabVersion& value);

        /** bytes written by another writer, as they are */
        void raw(const QByteArray& value) { mData.append(value); }

        const QByteArray& data() const { return mData; }

    private:
        QByteArray mData;
};


/**
 * Reads what UBCollabWriter wrote, from data that comes from another computer
 * and may be anything. Every read is checked against the bytes that are left
 * and against the limit given by the caller; the first failure is kept and
 * every later read returns a neutral value, so that a parser can read on and
 * check ok() once. Nothing is allocated before it is known to be there.
 */
class UBCollabReader
{
    public:
        explicit UBCollabReader(const QByteArray& data);

        quint8 u8();
        quint32 u32();
        qint32 i32();
        quint64 u64();
        /** a finite number; anything else (NaN, infinity) fails */
        double f64();
        bool boolean();
        QByteArray bytes(qsizetype maxLength);
        /** UTF-8 text of at most maxBytes bytes */
        QString string(qsizetype maxBytes);
        /** a uuid that is not null */
        QUuid uuid();
        /** a uuid, or the null uuid */
        QUuid optionalUuid();
        UBCollabVersion version();

        /**
         * A count of items that follow, at most max, and not more than the
         * remaining bytes can hold when every item takes at least minItemBytes.
         */
        quint32 count(quint32 max, qsizetype minItemBytes);

        bool ok() const { return mOk; }
        bool atEnd() const { return mPos == mData.size(); }
        qsizetype remaining() const { return mData.size() - mPos; }
        const QString& error() const { return mError; }

        /** marks the data as invalid; the reason is kept for the log */
        void fail(const QString& reason);

    private:
        bool take(qsizetype length);

        const QByteArray& mData;
        qsizetype mPos{0};
        bool mOk{true};
        QString mError;
};

#endif /* UBCOLLABWIRE_H_ */
