// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QString>

#include <KLocalizedString>

namespace kshadowlrc
{

/*!
 * The words that describe what the lyrics are doing, written once.
 *
 * It maps LyricEngine::State to a sentence: 0 idle, 1 loading, 2 found, 3 not
 * found, 4 error. LyricsController::lyricStatus hands the result to QML, and the
 * tray tool tip (system/controlservice.cpp) calls statusText() for the same
 * words, so the settings window and the tray can never tell a different story
 * than the engine. The header sits here, beside the engine whose state it words,
 * rather than in src/system/ -- that directory is the desktop-integration
 * surface, not the lyrics.
 *
 * Everything here returns an i18n source string, so with no catalogue installed
 * the result is the English text itself.
 */
namespace lyricstatus
{

/*! One calm line for \a state, the primary text of the tool tip and the window. */
inline QString stateText(int state)
{
    switch (state) {
    case 1:
        return i18nc("@info:status", "Looking for lyrics…");
    case 2:
        return i18nc("@info:status", "Lyrics found.");
    case 3:
        return i18nc("@info:status", "No lyrics for this song.");
    case 4:
        return i18nc("@info:status", "Something went wrong while looking for lyrics.");
    case 0:
    default:
        return i18nc("@info:status", "Nothing is playing.");
    }
}

/*!
 * The secondary line: the diagnostic string, and only when the resolution
 * really failed. Never the primary text -- it is for the person who wants to
 * know why, not the headline.
 */
inline QString errorDetailText(int state, const QString &lastError)
{
    return state == 4 ? lastError : QString();
}

/*!
 * The state line alone, or with the error detail stacked under it when there is
 * one. This is what the tray tool tip puts above its offset line: a tool tip
 * cannot style text, so it stacks both lines into one string. The settings
 * window styles them separately and so reads stateText() and errorDetailText()
 * on its own.
 */
inline QString statusText(int state, const QString &lastError)
{
    const QString text = stateText(state);
    const QString detail = errorDetailText(state, lastError);
    if (detail.isEmpty()) {
        return text;
    }
    return text + QLatin1Char('\n') + detail;
}

} // namespace lyricstatus

} // namespace kshadowlrc
