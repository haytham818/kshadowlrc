// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "settings.h"

#include <QLoggingCategory>

#include <KConfigSkeleton>

namespace kshadowlrc
{

namespace
{

/*!
 * The generated class emits a *Changed signal for every property but leaves
 * persistence to the caller. Wiring all of them to save() once, here, means an
 * assign-through-property (including from QML) is written to disk without the
 * settings UI having to know about KConfigSkeleton.
 */
void persistChangesOn(KShadowLrcSettings *settings)
{
    QObject::connect(settings, &KShadowLrcSettings::fontFamilyChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::fontSizeChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::textColorChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::outlineEnabledChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::displayModeChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::visibleLinesChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::edgeChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::marginChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::clickThroughChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::outputNameChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::lyricOffsetMsChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::syncIntervalMsChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::neteaseEnabledChanged, settings, &KConfigSkeleton::save);
    QObject::connect(settings, &KShadowLrcSettings::lrclibEnabledChanged, settings, &KConfigSkeleton::save);
}

} // namespace

KShadowLrcSettings *createSettings(QObject *parent)
{
    auto *settings = new KShadowLrcSettings(parent);
    persistChangesOn(settings);
    return settings;
}

KShadowLrcSettings *createSettings(const KSharedConfig::Ptr &config, QObject *parent)
{
    Q_ASSERT(config);

    auto *settings = new KShadowLrcSettings(parent);
    // The generated constructor already opened kshadowlrcrc in the default
    // location; rebind to the caller's file and re-read so the values reflect
    // it instead of the ones just read from the default location.
    settings->setSharedConfig(config);
    settings->load();
    persistChangesOn(settings);
    return settings;
}

} // namespace kshadowlrc
