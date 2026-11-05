/*
 * Copyright (C) 2015-2024 Département de l'Instruction Publique (DIP-SEM)
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



#include "UBBackgroundManager.h"

#include "core/UBSettings.h"
#include "frameworks/UBPlatformUtils.h"
#include "gui/UBBackgroundRuling.h"

#include <QDomDocument>
#include <QSvgGenerator>


// uuids of the rulings offered by default in the background palette
const QUuid UBBackgroundManager::blankBackground{QUuid::fromString(QStringLiteral("{84836d5a-1846-4c91-9ff9-f2c4d118364d}"))};
const QUuid UBBackgroundManager::linedBackground{QUuid::fromString(QStringLiteral("{0de33238-c30e-4396-8ec4-2bfc53304389}"))};
const QUuid UBBackgroundManager::grid1cmBackground{QUuid::fromString(QStringLiteral("{7493c8bb-6d98-4be6-966c-fbddf9eff529}"))};
const QUuid UBBackgroundManager::grid5mmBackground{QUuid::fromString(QStringLiteral("{1655ed23-9290-444a-86b8-4774a1b8f0be}"))};

static QStringList preferredBackgroundUuids()
{
    QStringList uuidList{UBSettings::settings()->value("Board/BackgroundRulingList").toStringList()};
    uuidList.removeAll(QString{});

    if (uuidList.isEmpty())
    {
        // fallback if neither OpenBoard.config nor the user settings contain a list
        uuidList << UBBackgroundManager::blankBackground.toString() << UBBackgroundManager::linedBackground.toString()
                 << UBBackgroundManager::grid1cmBackground.toString() << UBBackgroundManager::grid5mmBackground.toString();
    }

    return uuidList;
}

UBBackgroundManager::UBBackgroundManager(QObject *parent)
    : QObject{parent}
{
    scan();
}

void UBBackgroundManager::scan()
{
    // scan application and user directories for background rules
    const QString appDir{UBPlatformUtils::applicationTemplateDirectory() + "/background"};
    const QString usrDir{UBSettings::userDataDirectory() + "/background"};

    scan(appDir, false);
    scan(usrDir, true);
}

void UBBackgroundManager::addBackground(UBBackgroundRuling &background)
{
    if (background.isValid())
    {
        if (!this->background(background.uuid()))
        {
            mBackgrounds.append(background);

            // store in user background template directory
            const QString usrDir{UBSettings::userDataDirectory() + "/background/"};
            QDir dir;
            dir.mkpath(usrDir);

            QFile file{usrDir + background.uuid().toString() + ".xml"};

            if (!file.open(QFile::WriteOnly))
            {
                qWarning() << "Cannot open" << file.fileName();
                return;
            }

            QXmlStreamWriter writer{&file};

            writer.setAutoFormatting(true);
            writer.setAutoFormattingIndent(2);
            writer.writeStartDocument();
            background.toXml(writer);
            writer.writeEndDocument();

            file.close();

            emit backgroundListChanged();
        }
    }
    else
    {
        qWarning() << "Invalid background ruling" << background.description();
    }
}

void UBBackgroundManager::deleteBackground(const QUuid& uuid)
{
    const auto bg{background(uuid)};

    if (bg->isValid() && bg->isUserProvided())
    {
        mBackgrounds.removeAll(*bg);
        const QString usrDir{UBSettings::userDataDirectory() + "/background/"};
        QFile::remove(usrDir + uuid.toString() + ".xml");

        emit backgroundListChanged();
    }
}

const QList<const UBBackgroundRuling*> UBBackgroundManager::backgrounds() const
{
    QList<const UBBackgroundRuling*> backgroundList;

    QStringList uuidList{preferredBackgroundUuids()};

    // first add rulings from list
    for (const auto& uuid : uuidList)
    {
        const auto ruling = background(QUuid::fromString(uuid));

        if (ruling && ruling->isValid())
        {
            backgroundList << ruling;
        }
    }

    for (const auto& ruling : mBackgrounds)
    {
        if (!uuidList.contains(ruling.uuid().toString()))
        {
            backgroundList << &ruling;
        }
    }

    return backgroundList;
}

const QList<const UBBackgroundRuling*> UBBackgroundManager::preferredBackgrounds() const
{
    QList<const UBBackgroundRuling*> backgroundList;

    for (const auto& uuid : preferredBackgroundUuids())
    {
        const auto ruling = background(QUuid::fromString(uuid));

        if (ruling && ruling->isValid() && !backgroundList.contains(ruling))
        {
            backgroundList << ruling;
        }
    }

    if (backgroundList.isEmpty())
    {
        // never show an empty palette
        return backgrounds();
    }

    return backgroundList;
}

bool UBBackgroundManager::isPreferred(const QUuid& uuid) const
{
    const auto uuidList{preferredBackgroundUuids()};

    return std::any_of(uuidList.cbegin(), uuidList.cend(),
                       [&uuid](const QString& entry) { return QUuid::fromString(entry) == uuid; });
}

QAction *UBBackgroundManager::backgroundAction(const UBBackgroundRuling &background, bool dark)
{
    auto action = new QAction{this};
    action->setIcon(createButtonIcon(background, dark));
    const QString lang{QLocale().name().left(2)};
    const QString descr{background.description(lang)};
    action->setToolTip(descr);
    action->setProperty("uuid", background.uuid());
    action->setCheckable(true);
    return action;
}

void UBBackgroundManager::updateAction(QAction *action, bool dark) const
{
    const auto uuid{action->property("uuid").toUuid()};
    const auto bg{background(uuid)};

    if (bg)
    {
        action->setIcon(createButtonIcon(*bg, dark));
    }
}

const UBBackgroundRuling* UBBackgroundManager::background(const QUuid &uuid) const
{
    for (auto& background : std::as_const(mBackgrounds))
    {
        if (background.uuid() == uuid)
        {
            return &background;
        }
    }

    return nullptr;
}

const UBBackgroundRuling* UBBackgroundManager::guessBackground(bool crossed, bool ruled, bool intermediateLines) const
{
    for (auto& background : std::as_const(mBackgrounds))
    {
        if (background.isCrossed() == crossed
                && background.isRuled() == ruled
                && background.hasIntermediateLines() == intermediateLines)
        {
            return &background;
        }
    }

    return nullptr;
}

QPixmap UBBackgroundManager::createButtonPixmap(const UBBackgroundRuling& background, bool dark, bool on) const
{
    QByteArray bgSvg{renderToSvg(background, dark)};
    QDomDocument bgDoc;

    if (!bgDoc.setContent(bgSvg))
    {
        qWarning() << "Cannot load rendered ruling" << background.description();
        return {};
    }

    return createButtonPixmap(bgDoc, dark, on);
}

QIcon UBBackgroundManager::createButtonIcon(const UBBackgroundRuling &background, bool dark) const
{
    QByteArray bgSvg{renderToSvg(background, dark)};
    QDomDocument bgDoc;

    if (!bgDoc.setContent(bgSvg))
    {
        qWarning() << "Cannot load rendered ruling" << background.description();
        return {};
    }

    QIcon icon;

    QPixmap pix{createButtonPixmap(bgDoc, dark, false)};
    icon.addPixmap(pix, QIcon::Normal, QIcon::Off);

    pix = createButtonPixmap(bgDoc, dark, true);
    icon.addPixmap(pix, QIcon::Normal, QIcon::On);

    return icon;
}

void UBBackgroundManager::savePreferredBackgrounds(QList<QUuid>& uuidList)
{
    QStringList uuids;

    for (const auto& uuid : uuidList)
    {
        uuids << uuid.toString();
    }

    UBSettings::settings()->setValue("Board/BackgroundRulingList", uuids);
    emit preferredBackgroundChanged();
}

void UBBackgroundManager::scan(const QString &dirname, bool userProvided)
{
    const QDir dir{dirname};

    if (dir.exists())
    {
        const auto list{dir.entryInfoList({"*.xml"}, QDir::Files, QDir::Name)};

        for (const auto& entry : list)
        {
            UBBackgroundRuling bg;
            QFile file{entry.absoluteFilePath()};

            if (!file.open(QFile::ReadOnly))
            {
                continue;
            }

            QXmlStreamReader reader{&file};

            if (reader.readNextStartElement())
            {
                bg.parseXml(reader, userProvided);
            }

            if (bg.isValid())
            {
                mBackgrounds.append(bg);
            }
            else
            {
                qWarning() << "Error reading background definitions from" << file.fileName();
            }
        }
    }
}

QByteArray UBBackgroundManager::renderToSvg(const UBBackgroundRuling &background, bool dark) const
{
    QSvgGenerator generator;
    QBuffer buffer;
    generator.setOutputDevice(&buffer);
    generator.setSize(QSize(93, 75));
    generator.setViewBox(QRect(0, 0, 93, 75));

    QPainter painter;

    painter.begin(&generator);
    background.draw(&painter, {0, 0, 93, 75}, 0., {0, 0, 193, 75}, dark);
    painter.end();

    return buffer.buffer();
}

QPixmap UBBackgroundManager::createButtonPixmap(const QDomDocument& bgDoc, bool dark, bool on) const
{
    QString filename{"bgButtonTemplate"};

    filename += dark ? "Dark" : "Light";
    filename += on ? "On" : "Off";
    filename += ".svg";

    QFile templateFile{":/images/backgroundPalette/" + filename};

    if (!templateFile.open(QFile::ReadOnly))
    {
        qWarning() << "Cannot find template file" << templateFile.fileName();
        return {};
    }

    QDomDocument doc;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 5, 0))
    auto result = doc.setContent(&templateFile, QDomDocument::ParseOption::UseNamespaceProcessing);
#else
    auto result = doc.setContent(&templateFile, true);
#endif
    if (!result)
    {
        qWarning() << "Cannot load template file" << templateFile.fileName();
        return {};
    }

    // find <g> element with id="placeholder"
    auto plist{doc.documentElement().elementsByTagName("g")};

    QDomElement placeholder;

    for (int i = 0; i < plist.size(); ++i)
    {
        const auto item{plist.at(i).toElement()};

        if (item.attribute("id") == "placeholder")
        {
            placeholder = item;
            break;
        }
    }

    // now process background SVG
    // find first <g> element
    const auto bgElement{bgDoc.documentElement().firstChildElement("g")};

    // replace element in original document
    const auto bg{doc.importNode(bgElement, true)};
    placeholder.parentNode().replaceChild(bg, placeholder);

    QByteArray replaced{doc.toByteArray(2)};

    // render SVG to pixmap by using an QImageReader
    QBuffer buffer{&replaced};
    QImageReader reader{&buffer, "svg"};

    return QPixmap::fromImageReader(&reader);
}
