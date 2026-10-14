// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/matching.h"

#include <QSet>

#include <algorithm>

namespace kshadowlrc
{

namespace
{

QString joinedArtists(const QStringList &artists)
{
    return artists.join(QLatin1Char(','));
}

QSet<QString> bigrams(const QString &normalised)
{
    QSet<QString> set;
    const int count = normalised.size() - 1;
    for (int i = 0; i < count; ++i) {
        set.insert(normalised.mid(i, 2));
    }
    return set;
}

/*! Confidence above which a fuzzy hit outranks a mere duration match. */
constexpr double kFuzzyConfidence = 0.6;

} // namespace

QString normalizeForMatch(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber()) {
            out.append(c.toCaseFolded());
        }
    }
    return out;
}

double bigramSimilarity(const QString &a, const QString &b)
{
    const QSet<QString> left = bigrams(a);
    const QSet<QString> right = bigrams(b);
    const int denominator = left.size() + right.size();
    if (denominator == 0) {
        return a == b ? 1.0 : 0.0;
    }

    int intersection = 0;
    const QSet<QString> &smaller = left.size() <= right.size() ? left : right;
    const QSet<QString> &larger = left.size() <= right.size() ? right : left;
    for (const QString &gram : smaller) {
        if (larger.contains(gram)) {
            ++intersection;
        }
    }
    return 2.0 * intersection / denominator;
}

double weightedSimilarity(const TrackMeta &track, const SongInfo &candidate)
{
    const double title = bigramSimilarity(normalizeForMatch(track.title), normalizeForMatch(candidate.title));

    const bool hasAlbum = !track.album.trimmed().isEmpty();
    const bool hasArtists = !track.artists.isEmpty();

    if (hasArtists && hasAlbum) {
        const double singer = bigramSimilarity(normalizeForMatch(joinedArtists(track.artists)), normalizeForMatch(candidate.singer));
        const double album = bigramSimilarity(normalizeForMatch(track.album), normalizeForMatch(candidate.album));
        return title * 0.4 + album * 0.4 + singer * 0.2;
    }
    if (hasArtists) {
        const double singer = bigramSimilarity(normalizeForMatch(joinedArtists(track.artists)), normalizeForMatch(candidate.singer));
        return title * 0.7 + singer * 0.3;
    }
    if (hasAlbum) {
        const double album = bigramSimilarity(normalizeForMatch(track.album), normalizeForMatch(candidate.album));
        return title * 0.8 + album * 0.2;
    }
    return title;
}

bool withinDurationTolerance(const TrackMeta &track, const SongInfo &candidate, qint64 toleranceMs)
{
    if (track.lengthMs <= 0 || candidate.lengthMs <= 0) {
        return false;
    }
    const qint64 diff = track.lengthMs > candidate.lengthMs ? track.lengthMs - candidate.lengthMs : candidate.lengthMs - track.lengthMs;
    return diff <= toleranceMs;
}

SongMatch matchLikelyLyric(const TrackMeta &track, const SongInfo &candidate, qint64 toleranceMs)
{
    SongMatch match;
    match.score = weightedSimilarity(track, candidate);
    if (match.score >= kFuzzyConfidence) {
        match.rank = 0;
    } else if (withinDurationTolerance(track, candidate, toleranceMs)) {
        match.rank = 1;
    } else {
        match.rank = 2;
    }
    return match;
}

QList<RankedSong> rankCandidates(const TrackMeta &track, const QList<SongInfo> &candidates, int providerIndex, qint64 toleranceMs)
{
    QList<RankedSong> ranked;
    ranked.reserve(candidates.size());
    for (const SongInfo &candidate : candidates) {
        RankedSong entry;
        entry.song = candidate;
        entry.providerIndex = providerIndex;
        entry.match = matchLikelyLyric(track, candidate, toleranceMs);
        ranked.append(entry);
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const RankedSong &a, const RankedSong &b) {
        if (a.match.rank != b.match.rank) {
            return a.match.rank < b.match.rank;
        }
        return a.match.score > b.match.score;
    });
    return ranked;
}

} // namespace kshadowlrc
