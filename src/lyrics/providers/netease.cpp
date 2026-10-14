// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "providers/netease.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

Q_LOGGING_CATEGORY(lcNetease, "kshadowlrc.lyrics.netease")

namespace kshadowlrc
{
namespace
{

// The plaintext /api/ endpoints below are legacy and carry no compatibility
// promise. If they ever start requiring the encrypted path, the /weapi/ variant
// is built from these (kept for reference only; deliberately not implemented):
//   presetKey     = "0CoJUm6Qyw8W8jud"
//   iv            = "0102030405060708"
//   RSA exponent  = 65537
//   RSA modulus   = 1024-bit, starting e0b509f6259df8642dbc35662901477d...
// The params would be AES-CBC(presetKey, iv) then AES-CBC(randomKey, iv), and
// encrypted with RSA (no padding). That is exactly why all URL/body building
// and JSON parsing sits behind the small helpers rather than inline here.
constexpr char kSearchUrl[] = "https://music.163.com/api/cloudsearch/pc";
constexpr char kLyricUrl[] = "https://music.163.com/api/song/lyric";

constexpr int kDefaultSearchLimit = 30;
constexpr int kTransferTimeoutMs = 10000;
constexpr int kMaxAttempts = 3; //!< first try plus up to two retries
constexpr int kBackoffBaseMs = 500;
constexpr int kBackoffCapMs = 4000;

/*! Chrome-looking part after our own identifier, cheaply lowering the odds of
 * being classified as a bot. */
const QString &userAgent()
{
    static const QString agent = QStringLiteral(
                                     "KShadowLrc/%1 (+https://github.com/kshadowlrc/kshadowlrc) "
                                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                     "Chrome/120.0.0.0 Safari/537.36")
                                     .arg(QStringLiteral(KSHADOWLRC_VERSION_STRING));
    return agent;
}

/*! Strings a placeholder-only lyric reduces to; none of these is a real lyric. */
bool isPlaceholderLine(const QString &text)
{
    static const QStringList placeholders{
        QStringLiteral("暂无歌词"),
        QStringLiteral("暂无歌词。"),
        QStringLiteral("纯音乐，请欣赏"),
        QStringLiteral("纯音乐，请欣赏。"),
    };
    return placeholders.contains(text);
}

/*! Drop every `[...]` tag (timestamps and metadata) and trim what remains. */
QString stripTags(const QString &line)
{
    static const QRegularExpression tag(QStringLiteral("\\[[^\\]]*\\]"));
    QString text = line;
    text.remove(tag);
    return text.trimmed();
}

QString stringOrEmpty(const QJsonValue &value)
{
    return value.isString() ? value.toString() : QString();
}

qint64 jsonInteger(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toInteger();
    }
    if (value.isString()) {
        return value.toString().toLongLong();
    }
    return 0;
}

/*! NetEase ids are numbers but some endpoints stringify them; accept both. */
QString jsonId(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toInteger());
    }
    return {};
}

QString albumName(const QJsonValue &value)
{
    if (!value.isObject()) {
        return {}; // `al` is frequently null.
    }
    return stringOrEmpty(value.toObject().value(QStringLiteral("name")));
}

QString joinArtists(const QJsonValue &value)
{
    if (!value.isArray()) {
        return {};
    }
    QStringList names;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            continue;
        }
        const QString name = stringOrEmpty(entry.toObject().value(QStringLiteral("name")));
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    return names.join(QStringLiteral(", "));
}

bool isTrueFlag(const QJsonObject &root, const char *key)
{
    const QJsonValue value = root.value(QLatin1String(key));
    if (value.isBool()) {
        return value.toBool();
    }
    return value.isDouble() && value.toInteger() != 0;
}

QString lyricField(const QJsonObject &root, const char *key)
{
    const QJsonValue container = root.value(QLatin1String(key));
    if (!container.isObject()) {
        return {};
    }
    return stringOrEmpty(container.toObject().value(QStringLiteral("lyric")));
}

} // namespace

NeteaseProvider::NeteaseProvider(QObject *parent)
    : LyricProvider(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

NeteaseProvider::~NeteaseProvider() = default;

QString NeteaseProvider::name() const
{
    return QStringLiteral("netease");
}

QString NeteaseProvider::displayName() const
{
    return i18n("NetEase Cloud Music");
}

bool NeteaseProvider::isLikelySongId(const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }
    for (const QChar c : id) {
        if (c.unicode() < u'0' || c.unicode() > u'9') {
            return false;
        }
    }
    return true;
}

void NeteaseProvider::searchSong(quint64 requestId, const TrackMeta &track, int limit)
{
    if (buildSearchKeyword(track).isEmpty()) {
        Q_EMIT searchFailed(requestId, i18n("Not enough track metadata to search NetEase"));
        return;
    }
    Pending pending;
    pending.kind = Pending::Kind::Search;
    pending.requestId = requestId;
    pending.track = track;
    pending.limit = limit;
    dispatch(pending);
}

void NeteaseProvider::queryLyric(quint64 requestId, const QString &songId)
{
    if (songId.isEmpty()) {
        Q_EMIT lyricFailed(requestId, songId, i18n("Empty NetEase song id"));
        return;
    }
    Pending pending;
    pending.kind = Pending::Kind::Lyric;
    pending.requestId = requestId;
    pending.songId = songId;
    dispatch(pending);
}

void NeteaseProvider::dispatch(const Pending &pending)
{
    const ApiRequest api =
        pending.kind == Pending::Kind::Search ? buildSearchRequest(buildSearchKeyword(pending.track), pending.limit) : buildLyricRequest(pending.songId);

    QNetworkReply *reply = m_network->post(makeNetworkRequest(api), api.body);
    m_pending.insert(reply, pending);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onReplyFinished(reply);
    });
}

void NeteaseProvider::onReplyFinished(QNetworkReply *reply)
{
    const auto it = m_pending.find(reply);
    if (it == m_pending.end()) {
        reply->deleteLater();
        return;
    }
    const Pending pending = *it;
    m_pending.erase(it);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorString = reply->errorString();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    // 429/403 are throttling; retry a couple of times with a capped backoff.
    const bool throttled = status == 429 || status == 403;
    if (throttled && pending.attempts + 1 < kMaxAttempts) {
        Pending retry = pending;
        ++retry.attempts;
        const int delay = qMin(kBackoffBaseMs << pending.attempts, kBackoffCapMs);
        qCInfo(lcNetease) << "NetEase throttled the request (HTTP" << status << "), retrying in" << delay << "ms";
        QTimer::singleShot(delay, this, [this, retry] {
            dispatch(retry);
        });
        return;
    }
    if (throttled) {
        fail(pending, i18n("NetEase rejected the request (HTTP %1)", status));
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        fail(pending, networkErrorString);
        return;
    }

    // An empty or unparseable body carries no result and must never be read as
    // "no lyrics"; the parsers turn it into an error.
    if (pending.kind == Pending::Kind::Search) {
        QString error;
        const QList<SongInfo> songs = parseSearchBody(body, &error);
        if (!error.isEmpty()) {
            fail(pending, error);
            return;
        }
        qCInfo(lcNetease) << "search returned" << songs.size() << "candidates";
        Q_EMIT searchFinished(pending.requestId, songs);
    } else {
        QString error;
        const LyricStore store = parseLyricBody(body, &error);
        if (!error.isEmpty()) {
            fail(pending, error);
            return;
        }
        qCInfo(lcNetease) << "lyric for" << pending.songId << (store.hasAny() ? "found" : "empty");
        Q_EMIT lyricFinished(pending.requestId, pending.songId, store);
    }
}

void NeteaseProvider::fail(const Pending &pending, const QString &error)
{
    qCWarning(lcNetease) << "request failed:" << error;
    if (pending.kind == Pending::Kind::Search) {
        Q_EMIT searchFailed(pending.requestId, error);
    } else {
        Q_EMIT lyricFailed(pending.requestId, pending.songId, error);
    }
}

QNetworkRequest NeteaseProvider::makeNetworkRequest(const ApiRequest &api)
{
    QNetworkRequest request(api.url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Referer", "https://music.163.com/");
    request.setRawHeader("Cookie", "os=pc");
    request.setTransferTimeout(kTransferTimeoutMs);
    return request;
}

QString NeteaseProvider::buildSearchKeyword(const TrackMeta &track)
{
    QStringList parts;
    const QString title = track.title.trimmed();
    if (!title.isEmpty()) {
        parts.append(title);
    }
    const QString album = track.album.trimmed();
    if (!album.isEmpty()) {
        parts.append(album);
    }
    QStringList artists;
    for (const QString &artist : track.artists) {
        const QString trimmed = artist.trimmed();
        if (!trimmed.isEmpty()) {
            artists.append(trimmed);
        }
    }
    if (!artists.isEmpty()) {
        parts.append(artists.join(QLatin1Char('/')));
    }
    return parts.join(QLatin1Char(' ')).simplified();
}

NeteaseProvider::ApiRequest NeteaseProvider::buildSearchRequest(const QString &keyword, int limit)
{
    const int effectiveLimit = limit > 0 ? limit : kDefaultSearchLimit;
    QByteArray body = "s=";
    body += QUrl::toPercentEncoding(keyword);
    body += "&type=1&limit=";
    body += QByteArray::number(effectiveLimit);
    body += "&offset=0&total=true";
    return {QUrl(QString::fromLatin1(kSearchUrl)), body};
}

NeteaseProvider::ApiRequest NeteaseProvider::buildLyricRequest(const QString &songId)
{
    // lv/kv/tv are all mandatory. Without lv the API returns an empty lrc.lyric
    // while tlyric still comes back, which looks like "no lyrics" but is not.
    QByteArray body = "id=";
    body += QUrl::toPercentEncoding(songId);
    body += "&lv=-1&kv=-1&tv=-1";
    return {QUrl(QString::fromLatin1(kLyricUrl)), body};
}

QList<SongInfo> NeteaseProvider::parseSearchBody(const QByteArray &body, QString *error)
{
    if (error) {
        error->clear();
    }
    if (body.trimmed().isEmpty()) {
        if (error) {
            *error = i18n("NetEase returned an empty search response");
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = i18n("NetEase returned an invalid search response: %1", parseError.errorString());
        }
        return {};
    }

    // `code` is always 200 here, so it is deliberately ignored. A missing
    // `result`/`songs` is an empty result set, not an error.
    const QJsonObject result = doc.object().value(QStringLiteral("result")).toObject();
    const QJsonArray songs = result.value(QStringLiteral("songs")).toArray();

    QList<SongInfo> out;
    out.reserve(songs.size());
    for (const QJsonValue &value : songs) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject song = value.toObject();
        SongInfo info;
        info.id = jsonId(song.value(QStringLiteral("id")));
        if (info.id.isEmpty()) {
            continue; // unusable: the caller needs the id to fetch a lyric
        }
        info.title = stringOrEmpty(song.value(QStringLiteral("name")));
        info.album = albumName(song.value(QStringLiteral("al")));
        info.singer = joinArtists(song.value(QStringLiteral("ar")));
        info.lengthMs = jsonInteger(song.value(QStringLiteral("dt")));
        out.append(info);
    }
    return out;
}

LyricStore NeteaseProvider::parseLyricBody(const QByteArray &body, QString *error)
{
    if (error) {
        error->clear();
    }
    if (body.trimmed().isEmpty()) {
        if (error) {
            *error = i18n("NetEase returned an empty lyric response");
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = i18n("NetEase returned an invalid lyric response: %1", parseError.errorString());
        }
        return {};
    }
    const QJsonObject root = doc.object();

    // `code` is always 200, even for a missing or copyright-blocked song. These
    // flags are the only signal that there is no lyric, and they mean "no
    // lyrics", not an error.
    if (isTrueFlag(root, "uncollected") || isTrueFlag(root, "nolyric") || isTrueFlag(root, "pureMusic")) {
        return {};
    }

    LyricStore store;
    store.lyric = lyricField(root, "lrc");
    store.tlyric = lyricField(root, "tlyric");

    // A placeholder ("[00:00.00]暂无歌词") or a translation carrying timestamps
    // but no text is not a lyric.
    if (lyricTextHasNoContent(store.lyric)) {
        store.lyric.clear();
    }
    if (lyricTextHasNoContent(store.tlyric)) {
        store.tlyric.clear();
    }
    return store;
}

bool NeteaseProvider::lyricTextHasNoContent(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString stripped = stripTags(line);
        if (stripped.isEmpty()) {
            continue;
        }
        if (!isPlaceholderLine(stripped)) {
            return false;
        }
    }
    return true;
}

} // namespace kshadowlrc
