// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrics/provider.h"

#include <QByteArray>
#include <QList>

#include <QObject>

class QNetworkAccessManager;

namespace kshadowlrc
{

/*!
 * LRCLib (https://lrclib.net) lyric provider.
 *
 * Searches with /api/search, answers a fully-identified track with a single
 * /api/get lookup, and fetches a lyric with /api/get/{id}. The response bodies
 * are parsed by \c parseSearchResponse / \c parseGetResponse, which are static
 * and pure so tests can drive them from a saved fixture without the network.
 *
 * A 404 is "not found", never an error: it becomes an empty search result or an
 * empty lyric store. The \c *Failed signals are reserved for transport/protocol
 * failures.
 */
class LRCLibProvider : public LyricProvider
{
    Q_OBJECT

public:
    explicit LRCLibProvider(QObject *parent = nullptr);
    ~LRCLibProvider() override;

    QString name() const override;
    QString displayName() const override;

    void searchSong(quint64 requestId, const TrackMeta &track, int limit) override;
    void queryLyric(quint64 requestId, const QString &songId) override;
    bool isLikelySongId(const QString &id) const override;

    /*! Map a /api/search array (or a single object) body to candidates. */
    static QList<SongInfo> parseSearchResponse(const QByteArray &body, bool *ok = nullptr);

    /*! Result of parsing a /api/get body. */
    struct GetResponse {
        bool found = false; //!< a lyric record was present
        bool instrumental = false; //!< the record marks itself as instrumental
        LyricStore store; //!< raw LRC, empty for instrumental
    };

    /*! Map a /api/get body to a lyric store. */
    static GetResponse parseGetResponse(const QByteArray &body, bool *ok = nullptr);

private:
    QNetworkAccessManager *m_network = nullptr;
};

} // namespace kshadowlrc
