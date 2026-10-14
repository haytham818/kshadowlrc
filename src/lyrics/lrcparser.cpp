// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/lrcparser.h"

#include <QHash>

#include <algorithm>

namespace kshadowlrc
{

namespace
{

bool allDigits(const QString &s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (const QChar c : s) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
}

/*! Turn the fractional part of a timestamp into milliseconds.
 *
 * The digits are read as a decimal fraction of a second, so "5" -> 500,
 * "05" -> 50, "014" -> 14. */
int fractionToMs(const QString &fraction)
{
    if (fraction.isEmpty()) {
        return 0;
    }
    QString padded = fraction;
    while (padded.size() < 3) {
        padded.append(QLatin1Char('0'));
    }
    if (padded.size() > 3) {
        padded.truncate(3);
    }
    bool ok = false;
    const int value = padded.toInt(&ok);
    return ok ? value : 0;
}

/*! Parse the content of a bracket tag as a timestamp.
 *
 * Accepted: minutes ':' seconds [('.' | ':') fraction], with 1-3 digit minutes,
 * 1-2 digit seconds and 1-3 digit fraction. Anything else (metadata, text) is
 * rejected. */
bool parseTimestamp(const QString &content, qint64 &outMs)
{
    const QStringList colon = content.split(QLatin1Char(':'));
    if (colon.size() < 2 || colon.size() > 3) {
        return false;
    }
    if (!allDigits(colon.at(0))) {
        return false;
    }
    bool ok = false;
    const qint64 minutes = colon.at(0).toLongLong(&ok);
    if (!ok) {
        return false;
    }

    QString seconds;
    QString fraction;
    if (colon.size() == 2) {
        const QStringList dot = colon.at(1).split(QLatin1Char('.'));
        if (dot.size() > 2) {
            return false;
        }
        seconds = dot.at(0);
        fraction = dot.size() == 2 ? dot.at(1) : QString();
    } else {
        seconds = colon.at(1);
        fraction = colon.at(2);
    }

    if (!allDigits(seconds) || seconds.size() > 2) {
        return false;
    }
    if (!fraction.isEmpty() && (!allDigits(fraction) || fraction.size() > 3)) {
        return false;
    }

    const qint64 secs = seconds.toLongLong(&ok);
    if (!ok) {
        return false;
    }
    outMs = ((minutes * 60) + secs) * 1000 + fractionToMs(fraction);
    return true;
}

bool isExtendedTag(const QString &tag)
{
    // "<mm:ss.ms>": 7..11 characters including the brackets.
    const int len = tag.size();
    if (len < 7 || len > 11) {
        return false;
    }
    if (tag.at(0) != QLatin1Char('<') || tag.at(len - 1) != QLatin1Char('>')) {
        return false;
    }
    const QString content = tag.mid(1, len - 2);

    int pos = 0;
    const auto consumeDigits = [&content](int &p) {
        const int start = p;
        while (p < content.size() && content.at(p).isDigit()) {
            ++p;
        }
        return p - start;
    };

    const int minutes = consumeDigits(pos);
    if (minutes < 1 || minutes > 2 || pos >= content.size() || content.at(pos) != QLatin1Char(':')) {
        return false;
    }
    ++pos;

    const int seconds = consumeDigits(pos);
    if (seconds < 1 || seconds > 2 || pos >= content.size() || content.at(pos) != QLatin1Char('.')) {
        return false;
    }
    ++pos;

    const int millis = consumeDigits(pos);
    if (millis < 1 || millis > 3 || pos != content.size()) {
        return false;
    }
    return true;
}

QStringList splitLines(const QString &text)
{
    QString normalised = text;
    normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalised.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalised.split(QLatin1Char('\n'));
}

} // namespace

QString stripExtendedTimestamps(const QString &text)
{
    if (!text.contains(QLatin1Char('<')) || !text.contains(QLatin1Char('>'))) {
        return text;
    }

    QString out;
    out.reserve(text.size());
    int pos = 0;
    while (pos < text.size()) {
        const int tagStart = text.indexOf(QLatin1Char('<'), pos);
        if (tagStart < 0) {
            break;
        }
        const int tagEnd = text.indexOf(QLatin1Char('>'), tagStart + 1);
        if (tagEnd < 0) {
            break;
        }
        const QString tag = text.mid(tagStart, tagEnd - tagStart + 1);
        out.append(text.mid(pos, tagStart - pos));
        if (!isExtendedTag(tag)) {
            out.append(tag);
        }
        pos = tagEnd + 1;
    }
    out.append(text.mid(pos));
    return out;
}

Lyric parseLrc(const QString &text)
{
    Lyric result;

    if (text.trimmed().isEmpty()) {
        return result;
    }

    QList<LyricLine> timedLines;
    QStringList plainLines;

    const QStringList rawLines = splitLines(text);
    for (const QString &rawLine : rawLines) {
        const QString line = rawLine.endsWith(QLatin1Char('\r')) ? rawLine.left(rawLine.size() - 1) : rawLine;
        if (!line.startsWith(QLatin1Char('['))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                plainLines.append(trimmed);
            }
            continue;
        }

        // Scan the leading run of bracket tags. Timestamps accumulate; a
        // non-timestamp leading tag is metadata and is skipped.
        QList<qint64> timestamps;
        int pos = 0;
        while (pos < line.size() && line.at(pos) == QLatin1Char('[')) {
            const int close = line.indexOf(QLatin1Char(']'), pos + 1);
            if (close < 0) {
                break;
            }
            const QString content = line.mid(pos + 1, close - pos - 1);
            qint64 ms = 0;
            if (parseTimestamp(content, ms)) {
                timestamps.append(ms);
            }
            pos = close + 1;
        }

        if (timestamps.isEmpty()) {
            // Metadata-only or malformed line: dropped.
            continue;
        }

        const QString body = stripExtendedTimestamps(line.mid(pos)).trimmed();
        for (const qint64 ms : timestamps) {
            LyricLine lyricLine;
            lyricLine.text = body;
            lyricLine.startMs = ms;
            timedLines.append(lyricLine);
        }
    }

    if (!timedLines.isEmpty()) {
        std::stable_sort(timedLines.begin(), timedLines.end(), [](const LyricLine &a, const LyricLine &b) {
            return a.startMs < b.startMs;
        });
        result.kind = LyricKind::Timed;
        result.lines = std::move(timedLines);
        return result;
    }

    result.kind = LyricKind::NoTimestamp;
    for (const QString &plain : plainLines) {
        LyricLine lyricLine;
        lyricLine.text = plain;
        lyricLine.startMs = 0;
        result.lines.append(lyricLine);
    }
    return result;
}

int findNextLyric(const Lyric &lyric, qint64 elapsedMs)
{
    int found = -1;
    for (int i = 0; i < lyric.lines.size(); ++i) {
        if (lyric.lines.at(i).startMs <= elapsedMs) {
            found = i;
        } else {
            break;
        }
    }
    return found;
}

Lyric pairTranslation(const Lyric &origin, const Lyric &translation)
{
    if (origin.kind != LyricKind::Timed || origin.lines.isEmpty()) {
        return Lyric{};
    }

    QHash<qint64, QString> byTimestamp;
    for (const LyricLine &line : translation.lines) {
        const QString text = line.text.trimmed();
        if (text.isEmpty() || byTimestamp.contains(line.startMs)) {
            continue;
        }
        byTimestamp.insert(line.startMs, text);
    }

    if (byTimestamp.isEmpty()) {
        return Lyric{};
    }

    Lyric result;
    result.kind = LyricKind::Timed;
    result.lines.reserve(origin.lines.size());
    int matched = 0;
    for (const LyricLine &line : origin.lines) {
        const QString text = byTimestamp.value(line.startMs);
        if (!text.isEmpty()) {
            ++matched;
        }
        LyricLine translated;
        translated.text = text;
        translated.startMs = line.startMs;
        result.lines.append(translated);
    }

    if (matched == 0) {
        // Nothing lined up with the original: report "no translation".
        return Lyric{};
    }
    return result;
}

} // namespace kshadowlrc
