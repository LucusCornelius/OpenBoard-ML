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

#include "UBCollabCrypto.h"

#include <QList>
#include <QtEndian>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <climits>
#include <cstring>

namespace
{
    const char sAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    // the code is 24 random characters and one check character
    const int sRandomCharacters = 24;
    const int sCodeCharacters = sRandomCharacters + 1;
    const int sGroupSize = 5;

    // labels of what is derived from the code; changing one makes another, incompatible protocol
    const QByteArray sCodeSalt("OpenBoard-ML collab v1");
    const QByteArray sMasterInfo("message key");
    const QByteArray sRoomInfo("relay room id");
    const QByteArray sSenderInfo("sender key ");
    const QByteArray sAadLabel("OBML");

    int symbolValue(char32_t c)
    {
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';

        // the look-alikes are read as what they look like
        if (c == 'O')
            c = '0';
        else if (c == 'I' || c == 'L')
            c = '1';

        for (int i = 0; i < 32; ++i)
        {
            if (sAlphabet[i] == char(c) && c < 128)
                return i;
        }
        return -1;
    }

    bool isSeparator(char32_t c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == '_' || c == '.'
            || c == 0x00A0 || (c >= 0x2010 && c <= 0x2015) || c == 0x2212 || c == 0x202F || c == 0x2009;
    }

    // weighted sum modulo a prime: catches every changed character and every swap of two neighbours
    int checkValue(const int* values, int count)
    {
        int sum = 0;
        for (int i = 0; i < count; ++i)
            sum = (sum + (i + 1) * values[i]) % 31;
        return sum;
    }

    QString formatCode(const int* values)
    {
        QString code;
        for (int i = 0; i < sCodeCharacters; ++i)
        {
            if (i > 0 && i % sGroupSize == 0)
                code += QLatin1Char('-');
            code += QLatin1Char(sAlphabet[values[i]]);
        }
        return code;
    }

    QByteArray nonceFor(quint64 sequence)
    {
        QByteArray nonce(12, '\0');
        qToBigEndian(sequence, nonce.data() + 4);
        return nonce;
    }

    QByteArray aadFor(quint32 from, QByteArrayView header)
    {
        QByteArray aad = sAadLabel;
        aad.resize(sAadLabel.size() + 4);
        qToBigEndian(from, aad.data() + sAadLabel.size());
        aad.append(header.data(), header.size());
        return aad;
    }

    bool fitsInInt(qsizetype size)
    {
        return size >= 0 && size <= INT_MAX;
    }
}

const char* UBCollabCrypto::codeAlphabet()
{
    return sAlphabet;
}

QByteArray UBCollabCrypto::randomBytes(int count)
{
    QByteArray bytes(count, '\0');
    if (count > 0 && RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), count) != 1)
        return QByteArray();
    return bytes;
}

QByteArray UBCollabCrypto::hkdf(const QByteArray& secret, const QByteArray& salt, const QByteArray& info, int length)
{
    QByteArray result;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx)
        return result;

    QByteArray out(length, '\0');
    size_t outLength = size_t(length);
    if (EVP_PKEY_derive_init(ctx) == 1
        && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) == 1
        && EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char*>(salt.constData()), int(salt.size())) == 1
        && EVP_PKEY_CTX_set1_hkdf_key(ctx, reinterpret_cast<const unsigned char*>(secret.constData()), int(secret.size())) == 1
        && EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char*>(info.constData()), int(info.size())) == 1
        && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(out.data()), &outLength) == 1
        && outLength == size_t(length))
    {
        result = out;
    }
    EVP_PKEY_CTX_free(ctx);
    return result;
}

bool UBCollabCrypto::encrypt(const QByteArray& key, const QByteArray& nonce, QByteArrayView aad,
                             QByteArrayView plaintext1, QByteArrayView plaintext2, char* out)
{
    if (key.size() != KeySize || nonce.size() != 12 || !fitsInInt(aad.size())
        || !fitsInInt(plaintext1.size()) || !fitsInInt(plaintext2.size())
        || plaintext1.size() + plaintext2.size() > INT_MAX - TagSize)
    {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    unsigned char* o = reinterpret_cast<unsigned char*>(out);
    int length = 0;
    int written = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key.constData()),
                                 reinterpret_cast<const unsigned char*>(nonce.constData())) == 1
        && EVP_EncryptUpdate(ctx, nullptr, &length, reinterpret_cast<const unsigned char*>(aad.data()), int(aad.size())) == 1;

    if (ok && !plaintext1.isEmpty())
    {
        ok = EVP_EncryptUpdate(ctx, o + written, &length,
                               reinterpret_cast<const unsigned char*>(plaintext1.data()), int(plaintext1.size())) == 1;
        written += length;
    }
    if (ok && !plaintext2.isEmpty())
    {
        ok = EVP_EncryptUpdate(ctx, o + written, &length,
                               reinterpret_cast<const unsigned char*>(plaintext2.data()), int(plaintext2.size())) == 1;
        written += length;
    }
    if (ok)
    {
        ok = EVP_EncryptFinal_ex(ctx, o + written, &length) == 1;
        written += length;
    }
    ok = ok && written == int(plaintext1.size() + plaintext2.size())
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TagSize, o + written) == 1;

    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool UBCollabCrypto::decrypt(const QByteArray& key, const QByteArray& nonce, QByteArrayView aad,
                             QByteArrayView sealed, QByteArray& plaintext)
{
    plaintext.clear();
    if (key.size() != KeySize || nonce.size() != 12 || !fitsInInt(aad.size())
        || sealed.size() < TagSize || !fitsInInt(sealed.size()))
    {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    const int cipherLength = int(sealed.size()) - TagSize;
    QByteArray out(cipherLength, Qt::Uninitialized);
    unsigned char* o = reinterpret_cast<unsigned char*>(out.data());
    QByteArray tag(sealed.data() + cipherLength, TagSize);
    int length = 0;
    int written = 0;

    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key.constData()),
                                 reinterpret_cast<const unsigned char*>(nonce.constData())) == 1
        && EVP_DecryptUpdate(ctx, nullptr, &length, reinterpret_cast<const unsigned char*>(aad.data()), int(aad.size())) == 1;

    if (ok && cipherLength > 0)
    {
        ok = EVP_DecryptUpdate(ctx, o, &length, reinterpret_cast<const unsigned char*>(sealed.data()), cipherLength) == 1;
        written += length;
    }
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TagSize, tag.data()) == 1;

    // the tag is checked here; until then nothing of the plaintext may be used
    if (ok)
    {
        ok = EVP_DecryptFinal_ex(ctx, o + written, &length) == 1;
        written += length;
    }
    EVP_CIPHER_CTX_free(ctx);

    if (!ok || written != cipherLength)
        return false;

    plaintext = out;
    return true;
}

QString UBCollabCrypto::newCode()
{
    // 15 bytes are 120 bits, five for each of the 24 random characters
    const QByteArray random = randomBytes(15);
    if (random.size() != 15)
        return QString();

    int values[sCodeCharacters];
    quint32 buffer = 0;
    int bits = 0;
    int count = 0;
    for (char byte : random)
    {
        buffer = (buffer << 8) | quint8(byte);
        bits += 8;
        while (bits >= 5)
        {
            bits -= 5;
            values[count++] = int((buffer >> bits) & 31);
        }
    }
    values[sRandomCharacters] = checkValue(values, sRandomCharacters);
    return formatCode(values);
}

QString UBCollabCrypto::normalizedCode(const QString& text)
{
    int values[sCodeCharacters];
    int count = 0;

    const QList<uint> characters = text.toUcs4();
    for (uint c : characters)
    {
        if (isSeparator(c))
            continue;

        const int value = symbolValue(c);
        if (value < 0 || count == sCodeCharacters)
            return QString();
        values[count++] = value;
    }

    if (count != sCodeCharacters || checkValue(values, sRandomCharacters) != values[sRandomCharacters])
        return QString();

    return formatCode(values);
}

static QByteArray codeSecret(const QString& code)
{
    // the derivations use the characters of the code without the separators
    QByteArray secret = code.toLatin1();
    secret.replace('-', QByteArray());
    return secret;
}

QByteArray UBCollabCrypto::masterKey(const QString& code)
{
    return hkdf(codeSecret(code), sCodeSalt, sMasterInfo, KeySize);
}

QByteArray UBCollabCrypto::roomId(const QString& code)
{
    return hkdf(codeSecret(code), sCodeSalt, sRoomInfo, 32).toHex();
}

QByteArray UBCollabCrypto::senderKey(const QByteArray& masterKey, const QByteArray& instance)
{
    return hkdf(masterKey, QByteArray(), sSenderInfo + instance, KeySize);
}

UBCollabSealer::UBCollabSealer(const QByteArray& masterKey)
    : mInstance(UBCollabCrypto::randomBytes(UBCollabCrypto::InstanceSize))
    , mSequence(0)
{
    if (mInstance.size() == UBCollabCrypto::InstanceSize && masterKey.size() == UBCollabCrypto::KeySize)
        mKey = UBCollabCrypto::senderKey(masterKey, mInstance);
}

bool UBCollabSealer::isValid() const
{
    return mKey.size() == UBCollabCrypto::KeySize;
}

QByteArray UBCollabSealer::seal(quint32 from, quint32 to, QByteArrayView part1, QByteArrayView part2, int reserve)
{
    const qint64 plainLength = qint64(part1.size()) + part2.size();
    if (!isValid() || reserve < 0 || mSequence == ~quint64(0)
        || plainLength > INT_MAX - reserve - UBCollabCrypto::Overhead)
    {
        return QByteArray();
    }

    const quint64 sequence = mSequence + 1;

    QByteArray out(reserve + UBCollabCrypto::Overhead + int(plainLength), Qt::Uninitialized);
    char* header = out.data() + reserve;
    std::memset(out.data(), 0, reserve);
    header[0] = char(UBCollabCrypto::Version);
    std::memcpy(header + 1, mInstance.constData(), UBCollabCrypto::InstanceSize);
    qToBigEndian(sequence, header + 1 + UBCollabCrypto::InstanceSize);
    qToBigEndian(to, header + 1 + UBCollabCrypto::InstanceSize + 8);

    const QByteArray aad = aadFor(from, QByteArrayView(header, UBCollabCrypto::HeaderSize));
    if (!UBCollabCrypto::encrypt(mKey, nonceFor(sequence), aad, part1, part2, header + UBCollabCrypto::HeaderSize))
        return QByteArray();

    // only a message that was sealed uses up its number
    mSequence = sequence;
    return out;
}

UBCollabOpener::UBCollabOpener(const QByteArray& masterKey)
    : mMasterKey(masterKey)
    , mSequence(0)
{
}

UBCollabOpener::Result UBCollabOpener::open(quint32 from, QByteArrayView sealed, QByteArray& plaintext, quint32& to)
{
    plaintext.clear();
    if (sealed.size() < UBCollabCrypto::Overhead)
        return Malformed;
    if (quint8(sealed[0]) != UBCollabCrypto::Version)
        return OtherVersion;

    const char* header = sealed.data();
    const QByteArray instance(header + 1, UBCollabCrypto::InstanceSize);
    const quint64 sequence = qFromBigEndian<quint64>(header + 1 + UBCollabCrypto::InstanceSize);
    const quint32 addressee = qFromBigEndian<quint32>(header + 1 + UBCollabCrypto::InstanceSize + 8);

    QByteArray key = mKey;
    if (!isBound())
        key = UBCollabCrypto::senderKey(mMasterKey, instance);
    else if (instance != mInstance)
    {
        // a key for that instance would be needed to know whether it is forged or a second sender
        return OtherInstance;
    }

    const QByteArray aad = aadFor(from, QByteArrayView(header, UBCollabCrypto::HeaderSize));
    if (!UBCollabCrypto::decrypt(key, nonceFor(sequence), aad, sealed.sliced(UBCollabCrypto::HeaderSize), plaintext))
        return Forged;

    // authentic, so the number is the sender's: it must go up
    if (sequence <= mSequence)
    {
        plaintext.clear();
        return Replayed;
    }

    if (!isBound())
    {
        mInstance = instance;
        mKey = key;
    }
    mSequence = sequence;
    to = addressee;
    return Opened;
}
