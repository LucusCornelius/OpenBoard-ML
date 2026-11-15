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

#ifndef UBCOLLABCRYPTO_H_
#define UBCOLLABCRYPTO_H_

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

/**
 * The encryption of a shared session, on OpenSSL (EVP).
 *
 * Everything is derived from the session code with HKDF-SHA256: a master key,
 * and separately the room id under which the relay knows the session. The
 * relay gets the room id, never the code or a key.
 *
 * Every participant encrypts with its own key, derived from the master key and
 * a random "instance" of 16 bytes that it picks when it enters a session, and
 * numbers its messages 1, 2, 3, ... The nonce of AES-256-GCM is that number,
 * so nonces never repeat under one key, whatever the relay does. The cleartext
 * header (version, instance, number, addressee) and the peer id of the sender
 * as the relay reports it are authenticated: a message that the relay altered,
 * attributed to another participant, sent to someone it was not meant for,
 * replayed or put out of order does not open.
 *
 * Sealed message: version (1) | instance (16) | number (8, big endian) |
 * addressee (4, big endian, 0 = everybody) | ciphertext | GCM tag (16).
 */
class UBCollabCrypto
{
    public:
        static constexpr quint8 Version = 1;
        static constexpr int KeySize = 32;
        static constexpr int InstanceSize = 16;
        static constexpr int HeaderSize = 1 + InstanceSize + 8 + 4;
        static constexpr int TagSize = 16;
        static constexpr int Overhead = HeaderSize + TagSize;

        /** the characters of a session code: digits and capitals without I, L, O and U */
        static const char* codeAlphabet();

        /** random bytes from OpenSSL's generator; empty when it fails */
        static QByteArray randomBytes(int count);

        /** HKDF with SHA-256 (RFC 5869); empty on failure */
        static QByteArray hkdf(const QByteArray& secret, const QByteArray& salt, const QByteArray& info, int length);

        /** AES-256-GCM; \a out becomes ciphertext and tag. The plaintext is the two parts after each other. */
        static bool encrypt(const QByteArray& key, const QByteArray& nonce, QByteArrayView aad,
                            QByteArrayView plaintext1, QByteArrayView plaintext2, char* out);
        static bool decrypt(const QByteArray& key, const QByteArray& nonce, QByteArrayView aad,
                            QByteArrayView sealed, QByteArray& plaintext);

        /** a new session code: 24 random characters (120 bits) and a check character, in groups of five */
        static QString newCode();

        /** the code in its standard form, or empty when \a text cannot be a code (also when the check fails) */
        static QString normalizedCode(const QString& text);

        /** derived from a normalized code */
        static QByteArray masterKey(const QString& code);
        static QByteArray roomId(const QString& code);   // 64 hexadecimal digits

        static QByteArray senderKey(const QByteArray& masterKey, const QByteArray& instance);
};

/** encrypts the messages of this participant */
class UBCollabSealer
{
    public:
        explicit UBCollabSealer(const QByteArray& masterKey);

        bool isValid() const;
        QByteArray instance() const { return mInstance; }
        quint64 lastSequence() const { return mSequence; }

        /**
         * Seals the plaintext (the two parts after each other) from \a from to
         * \a to. The result starts with \a reserve zero bytes for the caller.
         * Empty on failure.
         */
        QByteArray seal(quint32 from, quint32 to, QByteArrayView part1, QByteArrayView part2, int reserve = 0);

    private:
        QByteArray mInstance;
        QByteArray mKey;
        quint64 mSequence;
};

/** opens the messages of one other participant */
class UBCollabOpener
{
    public:
        enum Result
        {
            Opened,
            Malformed,          // too short to be a sealed message
            OtherVersion,       // sealed by another version of the protocol
            Forged,             // does not authenticate: wrong key, or altered on the way
            OtherInstance,      // authenticates under another instance than the one of this participant so far
            Replayed            // the number is not higher than the last one: replayed or out of order
        };

        explicit UBCollabOpener(const QByteArray& masterKey);

        /** true after the first message that opened: from then on, this participant's instance is fixed */
        bool isBound() const { return !mInstance.isEmpty(); }

        Result open(quint32 from, QByteArrayView sealed, QByteArray& plaintext, quint32& to);

    private:
        QByteArray mMasterKey;
        QByteArray mInstance;
        QByteArray mKey;
        quint64 mSequence;
};

#endif /* UBCOLLABCRYPTO_H_ */
