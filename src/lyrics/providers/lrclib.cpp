// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/providers/lrclib.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace kshadowlrc
{

namespace
{

constexpr auto kBaseUrl = "https://lrclib.net";
constexpr int kRequestTimeoutMs = 8000;

const QString kUserAgent = [] {
    return QStringLiteral("KShadowLrc/" KSHADOWLRC_VERSION_STRING " (+https://github.com/haytham818/kshadowlrc)");
}();

qint64 secondsToMs(const QJsonValue &value)
{
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble() * 1000.0 + 0.5);
    }
    return 0;
}

QString idToString(const QJsonValue &value)
{
    if (value.isDouble()) {
        return QString::number(static_cast<qint64>(value.toDouble()));
    }
    return value.toString();
}

SongInfo songFromObject(const QJsonObject &object)
{
    SongInfo song;
    song.id = idToString(object.value(QStringLiteral("id")));
    song.title = object.value(QStringLiteral("trackName")).toString();
    if (song.title.isEmpty()) {
        song.title = object.value(QStringLiteral("name")).toString();
    }
    song.singer = object.value(QStringLiteral("artistName")).toString();
    song.album = object.value(QStringLiteral("albumName")).toString();
    song.lengthMs = secondsToMs(object.value(QStringLiteral("duration")));
    return song;
}

/*! True when the object is LRCLib's typed error body (statusCode/message). */
bool isErrorObject(const QJsonObject &object)
{
    return object.contains(QStringLiteral("statusCode")) && !object.contains(QStringLiteral("id"));
}

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

} // namespace

LRCLibProvider::LRCLibProvider(QObject *parent)
    : LyricProvider(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

LRCLibProvider::~LRCLibProvider() = default;

QString LRCLibProvider::name() const
{
    return QStringLiteral("lrclib");
}

QString LRCLibProvider::displayName() const
{
    return QStringLiteral("LRCLib");
}

bool LRCLibProvider::isLikelySongId(const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }
    bool ok = false;
    id.toLongLong(&ok);
    return ok;
}

QList<SongInfo> LRCLibProvider::parseSearchResponse(const QByteArray &body, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    QList<SongInfo> results;
    if (document.isArray()) {
        const QJsonArray array = document.array();
        results.reserve(array.size());
        for (const QJsonValue &value : array) {
            if (value.isObject()) {
                results.append(songFromObject(value.toObject()));
            }
        }
    } else if (document.isObject()) {
        const QJsonObject object = document.object();
        if (!isErrorObject(object)) {
            results.append(songFromObject(object));
        }
    } else {
        return {};
    }

    if (ok) {
        *ok = true;
    }
    return results;
}

LRCLibProvider::GetResponse LRCLibProvider::parseGetResponse(const QByteArray &body, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    const QJsonObject object = document.object();
    if (ok) {
        *ok = true;
    }
    if (isErrorObject(object) || !object.contains(QStringLiteral("id"))) {
        return {};
    }

    GetResponse response;
    response.found = true;
    response.instrumental = object.value(QStringLiteral("instrumental")).toBool();
    if (!response.instrumental) {
        const QString synced = object.value(QStringLiteral("syncedLyrics")).toString();
        const QString plain = object.value(QStringLiteral("plainLyrics")).toString();
        response.store.lyric = !synced.isEmpty() ? synced : plain;
    }
    return response;
}

void LRCLibProvider::searchSong(quint64 requestId, const TrackMeta &track, int limit)
{
    const QString title = track.title.trimmed();
    const QString artist = track.artists.join(QLatin1Char(' ')).trimmed();
    const QString album = track.album.trimmed();

    const bool detailed = !title.isEmpty() && !artist.isEmpty() && !album.isEmpty() && track.lengthMs > 0;

    QUrl url;
    QUrlQuery query;
    bool useGet = false;
    if (detailed) {
        useGet = true;
        url = QUrl(QString::fromLatin1(kBaseUrl) + QStringLiteral("/api/get"));
        query.addQueryItem(QStringLiteral("track_name"), title);
        query.addQueryItem(QStringLiteral("artist_name"), artist);
        query.addQueryItem(QStringLiteral("album_name"), album);
        query.addQueryItem(QStringLiteral("duration"), QString::number(static_cast<double>(track.lengthMs) / 1000.0, 'f', 0));
    } else {
        url = QUrl(QString::fromLatin1(kBaseUrl) + QStringLiteral("/api/search"));
        if (!title.isEmpty()) {
            query.addQueryItem(QStringLiteral("track_name"), title);
        }
        if (!artist.isEmpty()) {
            query.addQueryItem(QStringLiteral("artist_name"), artist);
        }
        if (!album.isEmpty()) {
            query.addQueryItem(QStringLiteral("album_name"), album);
        }
    }
    url.setQuery(query);

    if (!useGet && query.isEmpty()) {
        // Nothing to search with; degrade to an empty result, not a failure.
        Q_EMIT searchFinished(requestId, {});
        return;
    }

    QNetworkReply *reply = m_network->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, limit, detailed, track]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            if (detailed) {
                // The exact get missed; retry as a fuzzy search instead of
                // giving up, since the duration may simply not match exactly.
                TrackMeta relaxed = track;
                relaxed.lengthMs = 0;
                searchSong(requestId, relaxed, limit);
                return;
            }
            Q_EMIT searchFinished(requestId, {});
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            Q_EMIT searchFailed(requestId, reply->errorString());
            return;
        }

        bool ok = false;
        QList<SongInfo> results = parseSearchResponse(reply->readAll(), &ok);
        if (!ok) {
            Q_EMIT searchFailed(requestId, QStringLiteral("could not parse LRCLib search response"));
            return;
        }
        if (limit > 0 && results.size() > limit) {
            results = results.mid(0, limit);
        }
        Q_EMIT searchFinished(requestId, results);
    });
}

void LRCLibProvider::queryLyric(quint64 requestId, const QString &songId)
{
    if (!isLikelySongId(songId)) {
        Q_EMIT lyricFailed(requestId, songId, QStringLiteral("not a valid LRCLib song id"));
        return;
    }

    const QUrl url(QString::fromLatin1(kBaseUrl) + QStringLiteral("/api/get/") + songId);
    QNetworkReply *reply = m_network->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, songId]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            Q_EMIT lyricFinished(requestId, songId, {});
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            Q_EMIT lyricFailed(requestId, songId, reply->errorString());
            return;
        }

        bool ok = false;
        const GetResponse response = parseGetResponse(reply->readAll(), &ok);
        if (!ok) {
            Q_EMIT lyricFailed(requestId, songId, QStringLiteral("could not parse LRCLib lyric response"));
            return;
        }
        Q_EMIT lyricFinished(requestId, songId, response.store);
    });
}

} // namespace kshadowlrc
