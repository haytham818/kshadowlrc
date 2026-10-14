// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "provider.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkRequest>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace kshadowlrc
{

/*!
 * NetEase Cloud Music lyric provider.
 *
 * Talks to the plaintext `/api/` endpoints (search + lyric), which need no
 * authentication, cookies or crypto; the upstream Rust reference uses the
 * encrypted `/weapi/` path instead but we verified both return identical
 * results. See netease.cpp for the weapi constants kept for a future swap.
 *
 * Requests are asynchronous and used from the main thread only; every call
 * emits exactly one result signal carrying the caller's `requestId`.
 */
class NeteaseProvider : public LyricProvider
{
    Q_OBJECT

public:
    explicit NeteaseProvider(QObject *parent = nullptr);
    ~NeteaseProvider() override;

    QString name() const override;
    QString displayName() const override;

    void searchSong(quint64 requestId, const TrackMeta &track, int limit) override;
    void queryLyric(quint64 requestId, const QString &songId) override;

    bool isLikelySongId(const QString &id) const override;

private:
    // Only offline tests reach in here; the helpers are otherwise internal.
    friend struct NeteaseTestAccess;

    /*! URL and form body of one API call. Kept apart from the transport so the
     * request-building and body-parsing logic is exercised unchanged by tests. */
    struct ApiRequest {
        QUrl url;
        QByteArray body;
    };

    /*! One in-flight call, enough to retry it without the caller knowing. */
    struct Pending {
        enum class Kind {
            Search,
            Lyric,
        };

        Kind kind = Kind::Search;
        quint64 requestId = 0;
        int attempts = 0; //!< requests already dispatched, 0 on the first try
        TrackMeta track; //!< search
        int limit = 0; //!< search
        QString songId; //!< lyric
    };

    /*! "<title> <album> <artists joined by '/'>", empty parts omitted. */
    static QString buildSearchKeyword(const TrackMeta &track);
    static ApiRequest buildSearchRequest(const QString &keyword, int limit);
    static ApiRequest buildLyricRequest(const QString &songId);

    /*!
     * Parse a raw response body. On failure `error` (when non-null) receives a
     * non-empty message and the return value is empty; an empty result set is a
     * success, so callers must tell the two apart via `error`.
     */
    static QList<SongInfo> parseSearchBody(const QByteArray &body, QString *error);
    static LyricStore parseLyricBody(const QByteArray &body, QString *error);

    /*! True when the text carries no real lyric (only timestamps, a placeholder
     * like "暂无歌词", or nothing at all). */
    static bool lyricTextHasNoContent(const QString &text);

    void dispatch(const Pending &pending);
    void onReplyFinished(QNetworkReply *reply);
    void fail(const Pending &pending, const QString &error);
    static QNetworkRequest makeNetworkRequest(const ApiRequest &api);

    QNetworkAccessManager *m_network;
    QHash<QNetworkReply *, Pending> m_pending;
};

} // namespace kshadowlrc
