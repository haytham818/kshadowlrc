// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Through-the-controller checks for the pinned-lyric wiring. The sibling lanes
// cover the pin store and the engine's pinned override one level down; this is
// the only place that proves *LyricsController* actually reads the pin before
// setTrack() and surfaces it through trackQuery()/pinnedDescription().
//
// A track only reaches the controller through PlayerTracker, i.e. through
// MPRIS, so the harness mirrors playertracker_ignore_test.cpp: a FakePlayer on
// a private session bus brought up by a dbus-run-session re-exec, with the
// "SKIP: ..." prints kept byte-identical and the dbus-run-session lookup
// guarding the run.
//
// Two things keep it offline and side-effect free:
//   - a KShadowLrcSettings with both providers off, so an unpinned track walks
//     the sources, reaches no provider step and resolves to NotFound;
//   - HOME and the XDG_* locations pointed at a private QTemporaryDir *before*
//     the application object exists, so neither the pin store nor the cache can
//     land in the developer's real ~/.local/share.

#include "config/settings.h"
#include "lyrics/controller.h"
#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"
#include "lyrics/lyricsearch.h"
#include "lyrics/pinstore.h"
#include "player/playertracker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

using namespace kshadowlrc;

namespace
{

// Mirrors mprisplayer.h; kept local so the test does not depend on the wrapper.
const char kMprisObjectPath[] = "/org/mpris/MediaPlayer2";

// The two tracks the fake player will report. A carries a pin and a conflicting
// cache entry; B carries neither, so the ordinary walk decides.
TrackMeta fakeTrackA()
{
    TrackMeta track;
    track.title = QStringLiteral("Controller Pin Song");
    track.album = QStringLiteral("Controller Pin Album");
    track.artists = {QStringLiteral("Controller Pin Artist")};
    track.lengthMs = 180000;
    track.uniqueSongId = QStringLiteral("/kshadowlrc/controller_pin/track_a");
    return track;
}

TrackMeta fakeTrackB()
{
    TrackMeta track;
    track.title = QStringLiteral("Unpinned Controller Song");
    track.album = QStringLiteral("Unpinned Controller Album");
    track.artists = {QStringLiteral("Unpinned Controller Artist")};
    track.lengthMs = 180000;
    track.uniqueSongId = QStringLiteral("/kshadowlrc/controller_pin/track_b");
    return track;
}

/*! The MPRIS metadata that maps back onto \p track (see mprismeta.cpp). */
QVariantMap metadataFor(const TrackMeta &track)
{
    QVariantMap meta;
    meta.insert(QStringLiteral("xesam:title"), track.title);
    meta.insert(QStringLiteral("xesam:artist"), track.artists);
    meta.insert(QStringLiteral("xesam:album"), track.album);
    meta.insert(QStringLiteral("mpris:length"), track.lengthMs * 1000);
    meta.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(track.uniqueSongId)));
    return meta;
}

/*! The pin saved for track A, with distinctive text the assertions look for. */
pinstore::Pin pinForA()
{
    pinstore::Pin pin;
    pin.store.lyric = QStringLiteral("[00:01.00]Pinned controller line");
    pin.source = QStringLiteral("NetEase — Controller Pin / Artist");
    pin.provider = QStringLiteral("netease");
    pin.songId = QStringLiteral("ctrl-42");
    pin.title = QStringLiteral("Controller Pin Song");
    pin.singer = QStringLiteral("Controller Pin Artist");
    return pin;
}

/*!
 * A minimal in-process MPRIS player, playing from the moment it appears and
 * with a swappable track (the tracker re-reads Metadata on every poll).
 */
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ positionUs)

public:
    explicit FakePlayer(QObject *parent = nullptr)
        : QObject(parent)
    {
        show(fakeTrackA());
    }

    QString playbackStatus() const
    {
        return QStringLiteral("Playing");
    }

    QVariantMap metadata() const
    {
        return m_metadata;
    }

    qlonglong positionUs() const
    {
        return 0;
    }

    void show(const TrackMeta &track)
    {
        m_metadata = metadataFor(track);
    }

private:
    QVariantMap m_metadata;
};

} // namespace

class ControllerPinTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void inner();
};

void ControllerPinTest::initTestCase()
{
    // main() pointed HOME and the XDG locations at a private directory before
    // the application object existed; the stores must resolve under it.
    const QString home = QString::fromUtf8(qgetenv("HOME"));
    QVERIFY(!home.isEmpty());
    QVERIFY(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).startsWith(home));
    QVERIFY(pinstore::storePath().startsWith(home));
}

void ControllerPinTest::inner()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stdout) << "SKIP: session bus unavailable" << Qt::endl;
        return;
    }

    const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.kshadowlrc_controller_pin_test");
    if (!bus.registerService(serviceName)) {
        QFAIL(qPrintable(QStringLiteral("could not register %1").arg(serviceName)));
    }

    FakePlayer fake;
    if (!bus.registerObject(QString::fromLatin1(kMprisObjectPath), &fake, QDBusConnection::ExportAllContents)) {
        bus.unregisterService(serviceName);
        QFAIL("could not export the fake player");
    }

    const TrackMeta trackA = fakeTrackA();
    const TrackMeta trackB = fakeTrackB();
    const pinstore::Pin pinA = pinForA();
    const LyricStore cachedA = LyricStore{QStringLiteral("[00:01.00]Cached controller line"), QString()};

    // The pin and the conflicting cache entry exist before the player reports the
    // track, so the controller meets both on the first resolution.
    QVERIFY(pinstore::save(trackA, pinA));
    QVERIFY(LyricCache::store(trackA, cachedA));

    // Both providers off: an unpinned track walks the sources, reaches no
    // provider step and resolves to NotFound, with zero network.
    std::unique_ptr<KShadowLrcSettings> settings(createSettings());
    settings->setNeteaseEnabled(false);
    settings->setLrclibEnabled(false);
    QVERIFY(!settings->neteaseEnabled());
    QVERIFY(!settings->lrclibEnabled());

    LyricsController controller;
    controller.setSettings(settings.get());

    PlayerTracker tracker;
    tracker.setSyncIntervalMs(250);
    controller.setPlayerTracker(&tracker);

    QCoreApplication *app = QCoreApplication::instance();

    struct Step {
        int delayMs;
        std::function<void()> action;
    };
    std::vector<Step> steps;

    // (1)(2)(5a) The player reports track A. The pin answers it: the engine holds
    // the pinned text (not the conflicting cache entry), pinnedDescription() is
    // the stored source, and trackQuery() is "title artist".
    steps.push_back({700, [&] {
                         QVERIFY2(tracker.hasPlayer(), "the fake player is selected");
                         QVERIFY2(controller.engine()->state() == LyricEngine::State::Found, "the pinned lyric resolves to Found");
                         QCOMPARE(controller.engine()->lineText(0), QStringLiteral("Pinned controller line"));
                         QCOMPARE(controller.pinnedDescription(), pinA.source);
                         QCOMPARE(controller.trackQuery(), QStringLiteral("Controller Pin Song Controller Pin Artist"));

                         // The losing cache entry really is on disk ...
                         const auto cached = LyricCache::lookup(trackA);
                         QVERIFY2(cached.has_value(), "the conflicting cache entry is present");
                         if (cached) {
                             QCOMPARE(cached->store.lyric, cachedA.lyric);
                         }
                         // ... and the overlay shows the pin, never the cache.
                         QVERIFY2(controller.engine()->lineText(0) != cachedA.lyric, "the pinned text is shown, not the cached text");
                     }});

    // (4) Drop the pin of the track that is playing; normal resolution takes over.
    steps.push_back({0, [&] {
                         controller.forgetPinnedLyric();
                     }});

    steps.push_back({200, [&] {
                         QVERIFY2(!pinstore::lookup(trackA).has_value(), "the pin is gone from the store");
                         QVERIFY2(controller.pinnedDescription().isEmpty(), "pinnedDescription is empty after forgetting");
                         // Both providers are off and the fake track has no local
                         // file, so the ordinary walk runs and finds nothing.
                         QVERIFY2(controller.engine()->state() == LyricEngine::State::NotFound, "without the pin the ordinary walk runs and finds nothing");
                     }});

    // (3) The same walk for a track with no pin, no cache and no provider.
    steps.push_back({0, [&] {
                         fake.show(trackB);
                     }});

    steps.push_back({700, [&] {
                         QVERIFY2(controller.engine()->track().sameSong(trackB), "the tracker reported track B");
                         QVERIFY2(controller.engine()->state() == LyricEngine::State::NotFound,
                                  "an unpinned track with both providers off resolves to NotFound");
                         QVERIFY2(controller.pinnedDescription().isEmpty(), "track B has no pin, so pinnedDescription is empty");
                         QCOMPARE(controller.trackQuery(), QStringLiteral("Unpinned Controller Song Unpinned Controller Artist"));
                     }});

    // (6a) Pinning an index that has no results: nothing searched, so nothing to
    // pin. It must not crash, must not write anything, and must say so.
    steps.push_back({0, [&] {
                         QVERIFY(controller.search() != nullptr);
                         QVERIFY2(controller.search()->results().isEmpty(), "no search has run, so there are no rows");
                         controller.pinSearchResult(0);
                         QVERIFY2(!controller.pinStatus().isEmpty(), "pinning an out-of-range row reports a sentence");
                         QVERIFY2(!pinstore::lookup(trackB).has_value(), "nothing was written to the store for track B");
                     }});

    // (6b) Forgetting a song that has no pin is the same kind of no-op.
    steps.push_back({0, [&] {
                         controller.forgetPinnedLyric();
                         QVERIFY2(!controller.pinStatus().isEmpty(), "forgetting a song with no pin reports a sentence");
                         QVERIFY2(!pinstore::lookup(trackB).has_value(), "track B still has no pin");
                     }});

    // (5b) The player vanishes: trackQuery() must empty out again.
    steps.push_back({0, [&] {
                         bus.unregisterObject(QString::fromLatin1(kMprisObjectPath));
                         bus.unregisterService(serviceName);
                     }});

    steps.push_back({400, [&] {
                         QVERIFY2(!tracker.hasPlayer(), "the player is gone");
                         QVERIFY2(controller.trackQuery().isEmpty(), "trackQuery is empty again once the player disconnects");
                         QVERIFY2(controller.pinnedDescription().isEmpty(), "pinnedDescription is empty with nothing playing");
                     }});

    steps.push_back({0, [&] {
                         app->quit();
                     }});

    int index = 0;
    std::function<void()> runNext;
    runNext = [&]() {
        if (index >= static_cast<int>(steps.size())) {
            app->quit();
            return;
        }
        const Step step = steps[index++];
        QTimer::singleShot(step.delayMs, app, [&runNext, step] {
            step.action();
            runNext();
        });
    };
    QTimer::singleShot(0, app, runNext);
    tracker.start();
    app->exec();
}

int main(int argc, char **argv)
{
    // Private home/data/config/cache for whichever process actually runs the
    // test, set before any standard path (or KSharedConfig) is resolved. A
    // QTemporaryDir in the outer process covers the inner one through the
    // environment it inherits; each side therefore owns its own directory.
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary home directory\n");
        return 1;
    }
    qputenv("HOME", tempDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", (tempDir.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", (tempDir.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_CACHE_HOME", (tempDir.path() + QStringLiteral("/cache")).toUtf8());

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));

    const bool inner = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--inner");
    if (inner) {
        // Drop the --inner marker before QTest parses the arguments: it would
        // reject it as an unknown option and fail the run.
        for (int i = 1; i < argc - 1; ++i) {
            argv[i] = argv[i + 1];
        }
        --argc;
        QStandardPaths::setTestModeEnabled(true);
        ControllerPinTest tc;
        return QTest::qExec(&tc, argc, argv);
    }

    // Not yet on a private bus: re-exec under dbus-run-session so no real player
    // on the user's session can interfere (or be interfered with).
    const QString runner = QStandardPaths::findExecutable(QStringLiteral("dbus-run-session"));
    if (runner.isEmpty()) {
        QTextStream(stdout) << "SKIP: dbus-run-session not found" << Qt::endl;
        return 0;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(runner, {QStringLiteral("--"), QCoreApplication::applicationFilePath(), QStringLiteral("--inner")});
    if (!process.waitForStarted(5000)) {
        QTextStream(stdout) << "SKIP: could not start dbus-run-session" << Qt::endl;
        return 0;
    }
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 2;
}

#include "controller_pin_test.moc"
