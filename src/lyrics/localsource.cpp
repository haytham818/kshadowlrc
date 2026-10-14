// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/localsource.h"

#include "lyrics/localtags.h"

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QUrl>

namespace kshadowlrc
{

namespace
{

/*!
 * The text of a sidecar, or nullopt when it is absent, unreadable or blank.
 *
 * A blank sidecar is treated as "no sidecar": an empty file is something that
 * gets left behind by an editor, not a lyric.
 */
std::optional<QString> readSidecar(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray raw = file.readAll();
    if (raw.isEmpty()) {
        return std::nullopt;
    }

    QString text = QString::fromUtf8(raw);
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    if (text.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return text;
}

} // namespace

std::optional<QString> localPathFromSongId(const QString &uniqueSongId)
{
    const QUrl url(uniqueSongId);
    if (!url.isValid() || !url.isLocalFile()) {
        return std::nullopt;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return path;
}

std::optional<LyricStore> LocalFiles::load(const QString &localPath) const
{
    const QFileInfo info(localPath);
    const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    const QString lyricPath = base + QLatin1String(".lrc");
    const QString translationPath = base + QLatin1String(".zh.lrc");

    LyricStore store;

    // The sidecar is the copy the user edited last, so it wins.
    if (const std::optional<QString> lyric = readSidecar(lyricPath)) {
        store.lyric = *lyric;
        // A translation with no original is not a lyric, so .zh.lrc is only
        // honoured once .lrc itself yielded text.
        if (const std::optional<QString> translation = readSidecar(translationPath)) {
            store.tlyric = *translation;
        }
    } else if (const std::optional<QString> tag = readLyricsText(localPath)) {
        store.lyric = *tag;
    }

    if (!store.hasAny()) {
        return std::nullopt;
    }
    return store;
}

} // namespace kshadowlrc
