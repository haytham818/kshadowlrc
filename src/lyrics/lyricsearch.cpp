// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyricsearch.h"

#include "kshadowlrc.h"

#include <QVariantMap>

namespace kshadowlrc
{

LyricSearch::LyricSearch(QObject *parent)
    : QObject(parent)
{
}

void LyricSearch::setProviders(const QList<LyricProvider *> &inOrder)
{
    for (LyricProvider *provider : m_providers) {
        disconnect(provider, nullptr, this, nullptr);
    }

    m_providers.clear();
    for (LyricProvider *provider : inOrder) {
        if (provider) {
            m_providers.append(provider);
        }
    }
    for (LyricProvider *provider : m_providers) {
        connect(provider, &LyricProvider::searchFinished, this, &LyricSearch::onSearchFinished);
        connect(provider, &LyricProvider::searchFailed, this, &LyricSearch::onSearchFailed);
    }

    // A new provider list means new positions: abandon any in-flight search so
    // its late answers are dropped rather than attributed to the new list.
    resetState();
}

void LyricSearch::search(const QString &query, int limit)
{
    // A new search always invalidates the previous one.
    resetState();

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        // A blank query asks nothing; resetState() already cleared the state.
        return;
    }

    TrackMeta track;
    track.title = trimmed;

    for (int i = 0; i < m_providers.size(); ++i) {
        const quint64 requestId = m_nextRequestId++;
        m_pending.insert(requestId, {m_generation, i});
        m_providers.at(i)->searchSong(requestId, track, limit);
    }
    applyBusy(!m_pending.isEmpty());
}

void LyricSearch::onSearchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd() || it->generation != m_generation) {
        return; // stale or unknown answer
    }
    const int providerIndex = it->providerIndex;
    m_pending.erase(it);
    if (providerIndex < 0 || providerIndex >= m_providers.size()) {
        return;
    }

    LyricProvider *provider = m_providers.at(providerIndex);
    QVariantList rows;
    rows.reserve(results.size());
    for (const SongInfo &song : results) {
        QVariantMap row;
        row.insert(QStringLiteral("provider"), provider->name());
        row.insert(QStringLiteral("providerLabel"), provider->displayName());
        row.insert(QStringLiteral("id"), song.id);
        row.insert(QStringLiteral("title"), song.title);
        row.insert(QStringLiteral("singer"), song.singer);
        row.insert(QStringLiteral("album"), song.album);
        row.insert(QStringLiteral("lengthMs"), song.lengthMs);
        rows.append(row);
    }
    m_rowsByProvider[providerIndex] = rows;

    rebuildResults();
    applyBusy(!m_pending.isEmpty());
    finishIfComplete();
}

void LyricSearch::onSearchFailed(quint64 requestId, const QString &error)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd() || it->generation != m_generation) {
        return; // stale or unknown answer
    }
    const int providerIndex = it->providerIndex;
    m_pending.erase(it);
    if (providerIndex < 0 || providerIndex >= m_providers.size()) {
        return;
    }

    qCDebug(lcLyrics) << "manual search: provider failed:" << error;

    ++m_failedCount;
    // The error is the first failure in provider order, never the first to
    // arrive: the same rule the results follow, so the string does not depend
    // on which provider happened to answer first.
    if (m_firstFailureIndex < 0 || providerIndex < m_firstFailureIndex) {
        m_firstFailureIndex = providerIndex;
        m_firstFailure = error;
    }

    applyBusy(!m_pending.isEmpty());
    finishIfComplete();
}

void LyricSearch::resetState()
{
    ++m_generation;
    m_pending.clear();

    m_rowsByProvider.clear();
    for (int i = 0; i < m_providers.size(); ++i) {
        m_rowsByProvider.append(QVariantList{});
    }
    m_failedCount = 0;
    m_firstFailureIndex = -1;
    m_firstFailure.clear();

    applyResults(QVariantList{});
    applyError(QString{});
    applyBusy(false);
}

void LyricSearch::rebuildResults()
{
    QVariantList flattened;
    // Groups follow the position of the provider in setProviders(), never the
    // order the answers arrived in: an earlier provider's group always sits
    // above a later one's, even when it replied last. Inside a group the
    // provider's own order survives, because the person picking is the ranker.
    for (int i = 0; i < m_rowsByProvider.size(); ++i) {
        flattened += m_rowsByProvider.at(i);
    }
    applyResults(flattened);
}

void LyricSearch::finishIfComplete()
{
    if (!m_pending.isEmpty() || m_providers.isEmpty()) {
        return;
    }
    // Every provider has answered. Only a total failure is an error; a provider
    // that answered with an empty list succeeded.
    if (m_failedCount == m_providers.size()) {
        applyError(m_firstFailure);
    }
}

void LyricSearch::applyResults(const QVariantList &results)
{
    if (m_results == results) {
        return;
    }
    m_results = results;
    Q_EMIT resultsChanged();
}

void LyricSearch::applyBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void LyricSearch::applyError(const QString &error)
{
    if (m_error == error) {
        return;
    }
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace kshadowlrc
