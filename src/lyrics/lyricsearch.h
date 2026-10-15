// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "provider.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace kshadowlrc
{

/*!
 * The manual lyric search: one query fanned out to every enabled provider.
 *
 * The user opens this when the automatic match is wrong or missing, types a
 * title and picks the right candidate. Every provider is asked at once with the
 * bare query as a title; answers arrive asynchronously, one per provider.
 *
 * Results are grouped by provider, and a group's position is the provider's
 * position in the list handed to \ref setProviders() -- never the order answers
 * arrived in. A provider answering first does not jump ahead of one that
 * answers later; inside a group the provider's own order is kept verbatim,
 * because in a manual search the person picking is the one ranking.
 *
 * Each row is a QVariantMap with the keys \c provider (the stable name),
 * \c providerLabel (the translated name), \c id, \c title, \c singer, \c album
 * and \c lengthMs; \c id is passed back to \c LyricProvider::queryLyric().
 *
 * Starting a new search abandons the previous one: its late answers are dropped
 * rather than appended, and they never flip \ref busy back on. Only when every
 * provider of the current search fails does \ref error carry the first failure
 * string; a provider answering with an empty list is a successful, empty answer
 * and not an error. An empty or blank query asks nothing and clears the state.
 */
class LyricSearch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit LyricSearch(QObject *parent = nullptr);

    /*!
     * Providers are borrowed, never owned. Hand over only the enabled ones, in
     * the configured source order; their position here decides the grouping
     * order of \ref results. Null entries are ignored.
     */
    void setProviders(const QList<LyricProvider *> &inOrder);

    /*! Ask every provider for candidate songs matching \p query. */
    Q_INVOKABLE void search(const QString &query, int limit = 20);

    QVariantList results() const
    {
        return m_results;
    }
    bool busy() const
    {
        return m_busy;
    }
    QString error() const
    {
        return m_error;
    }

Q_SIGNALS:
    void resultsChanged();
    void busyChanged();
    void errorChanged();

private:
    /*! A search request in flight for the current generation. */
    struct PendingSearch {
        quint64 generation = 0;
        int providerIndex = -1;
    };

    void onSearchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results);
    void onSearchFailed(quint64 requestId, const QString &error);

    /*! Abandon the current search and clear every per-search slot. */
    void resetState();
    /*! Recompute the flattened, provider-ordered result list. */
    void rebuildResults();
    /*! Record the error once, and only once every provider has failed. */
    void finishIfComplete();

    void applyResults(const QVariantList &results);
    void applyBusy(bool busy);
    void applyError(const QString &error);

    QList<LyricProvider *> m_providers;
    // Per-provider rows of the current generation, indexed like m_providers.
    QList<QVariantList> m_rowsByProvider;

    QHash<quint64, PendingSearch> m_pending;
    quint64 m_generation = 0;
    quint64 m_nextRequestId = 1;

    int m_failedCount = 0;
    int m_firstFailureIndex = -1;
    QString m_firstFailure;

    QVariantList m_results;
    bool m_busy = false;
    QString m_error;
};

} // namespace kshadowlrc
