// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the lyric search window (src/qml/search.qml).
//
// It loads the file through a real QQmlApplicationEngine and gives it what the
// host gives it: the `lyricsController` and `lyricSearch` context properties
// (test-local stand-ins, so the search and the pinning are the test's to drive)
// and a KLocalizedQmlContext translator, so i18n() resolves. It checks that the
// file compiles, instantiates, shows itself and reports nothing to
// QQmlApplicationEngine::warnings - both with the host's objects and with none
// at all, which is what a bare engine does.
//
// It pins the mapping from a provider result to the row the list draws: the
// source label, title, singer, album, length, where one source's group begins,
// and whether the row can be pinned - with the reason when it cannot, which is
// what a row shows in place of its button. It pins the four states of the
// results area (searching, failed, nothing found, not asked yet), the two
// diagnostic lines that are never the headline, and that pinning and forgetting
// reach the controller with the index the row stands for.
//
// The mapping is read through the window's `rows` property rather than from the
// delegate objects: a Repeater's delegates carry no QObject parent, so
// findChildren() never reaches them - the same reason tests/settings_qml_test.cpp
// reads its Repeater rows through the model instead.
//
// The platform plugin is offscreen: no network, no GUI, nothing to click.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <cstdio>

// The QML lives in a resource compiled into the static kshadowlrc_lib; without
// an explicit initialiser the linker drops it and qrc:/qml/search.qml cannot be
// resolved. Q_INIT_RESOURCE must run in the global namespace, so this helper
// sits outside any namespace, exactly as src/app/application.cpp does it.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

// The wording this file pins, as written in src/qml/search.qml. The labels carry
// an objectName, so they are looked up through it rather than by text; keep the
// two in step if the wording ever changes.
const char kTrack[] = "光の中へ ヨルシカ";
const char kNothingPlaying[] = "Nothing is playing.";
const char kBusy[] = "Searching…";
const char kNothingFound[] = "Nothing found.";
const char kStartHint[] = "Search the song, then pick the lyric to use.";
const char kNoSongIdHint[] = "This result has no song id, so its lyric cannot be fetched.";

/*!
 * Stand-in for kshadowlrc::LyricsController, the object the host injects as the
 * `lyricsController` context property. Same shape as the real one for the parts
 * this window uses: the track that is playing, the pin already on that track,
 * the diagnostic of the last pin attempt, and the two invokables. Every call is
 * recorded, so the test can tell a row that is wired from one that is not.
 */
class StubLyricsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString trackQuery READ trackQuery NOTIFY changed)
    Q_PROPERTY(QString pinnedDescription READ pinnedDescription NOTIFY changed)
    Q_PROPERTY(QString pinStatus READ pinStatus NOTIFY pinStatusChanged)

public:
    using QObject::QObject;

    QString trackQuery() const
    {
        return m_trackQuery;
    }

    QString pinnedDescription() const
    {
        return m_pinnedDescription;
    }

    QString pinStatus() const
    {
        return m_pinStatus;
    }

    void setTrackQuery(const QString &query)
    {
        m_trackQuery = query;
        Q_EMIT changed();
    }

    void setPinnedDescription(const QString &description)
    {
        m_pinnedDescription = description;
        Q_EMIT changed();
    }

    void setPinStatus(const QString &status)
    {
        m_pinStatus = status;
        Q_EMIT pinStatusChanged();
    }

    Q_INVOKABLE void pinSearchResult(int index)
    {
        m_pinnedIndexes.append(index);
    }

    Q_INVOKABLE void forgetPinnedLyric()
    {
        ++m_forgetCount;
    }

    QList<int> pinnedIndexes() const
    {
        return m_pinnedIndexes;
    }

    int forgetCount() const
    {
        return m_forgetCount;
    }

Q_SIGNALS:
    void changed();
    void pinStatusChanged();

private:
    QString m_trackQuery;
    QString m_pinnedDescription;
    QString m_pinStatus;
    QList<int> m_pinnedIndexes;
    int m_forgetCount = 0;
};

/*!
 * Stand-in for kshadowlrc::LyricSearch, the object the host injects as the
 * `lyricSearch` context property. The test decides what came back and whether a
 * request is still in flight, and sees the queries the window sent.
 */
class StubLyricSearch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    using QObject::QObject;

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

    void setResults(const QVariantList &results)
    {
        m_results = results;
        Q_EMIT changed();
    }

    void setBusy(bool busy)
    {
        m_busy = busy;
        Q_EMIT changed();
    }

    void setError(const QString &error)
    {
        m_error = error;
        Q_EMIT changed();
    }

    Q_INVOKABLE void search(const QString &query)
    {
        m_queries.append(query);
    }

    QStringList queries() const
    {
        return m_queries;
    }

    void clearQueries()
    {
        m_queries.clear();
    }

Q_SIGNALS:
    void changed();

private:
    QVariantList m_results;
    bool m_busy = false;
    QString m_error;
    QStringList m_queries;
};

/*! One provider result, in the shape the interface freezes. */
QVariantMap providerResult(const QString &provider,
                           const QString &providerLabel,
                           const QString &id,
                           const QString &title,
                           const QString &singer,
                           const QString &album,
                           int lengthMs)
{
    QVariantMap result;
    result.insert(QStringLiteral("provider"), provider);
    result.insert(QStringLiteral("providerLabel"), providerLabel);
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("singer"), singer);
    result.insert(QStringLiteral("album"), album);
    result.insert(QStringLiteral("lengthMs"), lengthMs);
    return result;
}

} // namespace

class SearchQmlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void loadsWithoutWarnings();
    void prefillsTheSearchFieldWithThePlayingTrack();
    void mapsEachResultToItsRow();
    void explainsWhyARowCannotBePinned();
    void pinsAndForgetsThroughTheController();
    void keepsTheDiagnosticsOutOfTheHeadline();
    void walksTheResultsStates();
    void staysQuietWithoutTheHostObjects();

private:
    QObject *rootObject() const;
    QVariantList rows() const;
    QVariantMap rowAt(int index) const;
    QObject *byObjectName(const QString &name) const;
    bool flag(const char *name) const;
    QString labelText(const QString &name) const;
    void setSearched(bool searched) const;
    void clearHostObjects();
    int pinnableRowCount() const;
    QString pinHintText() const;

    QQmlApplicationEngine m_engine;
    StubLyricsController m_controller;
    StubLyricSearch m_search;
    QList<QQmlError> m_reported;
};

QObject *SearchQmlTest::rootObject() const
{
    return m_engine.rootObjects().isEmpty() ? nullptr : m_engine.rootObjects().constFirst();
}

QVariantList SearchQmlTest::rows() const
{
    return rootObject() ? rootObject()->property("rows").toList() : QVariantList();
}

QVariantMap SearchQmlTest::rowAt(int index) const
{
    return rows().value(index).toMap();
}

QObject *SearchQmlTest::byObjectName(const QString &name) const
{
    QObject *object = rootObject();
    return object ? object->findChild<QObject *>(name) : nullptr;
}

bool SearchQmlTest::flag(const char *name) const
{
    QObject *object = rootObject();
    return object && object->property(name).toBool();
}

QString SearchQmlTest::labelText(const QString &name) const
{
    QObject *label = byObjectName(name);
    return label ? label->property("text").toString() : QString();
}

// The window's one piece of local state: set to false so a slot can start from
// "no search asked for yet" without depending on the order the slots run in.
void SearchQmlTest::setSearched(bool searched) const
{
    QObject *object = rootObject();
    if (object) {
        object->setProperty("searched", searched);
    }
}

// Put the host's objects back to a quiet starting point: a track is playing, no
// search has answered anything, nothing is pinned and nothing has been reported.
void SearchQmlTest::clearHostObjects()
{
    m_search.setResults(QVariantList());
    m_search.setBusy(false);
    m_search.setError(QString());
    m_search.clearQueries();
    m_controller.setPinStatus(QString());
    m_controller.setPinnedDescription(QString());
    setSearched(false);
}

int SearchQmlTest::pinnableRowCount() const
{
    int count = 0;
    const QVariantList all = rows();
    for (const QVariant &row : all) {
        count += row.toMap().value(QStringLiteral("pinnable")).toBool() ? 1 : 0;
    }
    return count;
}

// The reasons shown in place of a pin button, gathered from every row that
// carries one.
QString SearchQmlTest::pinHintText() const
{
    QStringList hints;
    const QVariantList all = rows();
    for (const QVariant &row : all) {
        const QString hint = row.toMap().value(QStringLiteral("pinHint")).toString();
        if (!hint.isEmpty() && !hints.contains(hint)) {
            hints.append(hint);
        }
    }
    return hints.join(QLatin1Char('\n'));
}

void SearchQmlTest::initTestCase()
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kshadowlrc"));

    // As src/app/application.cpp: set only a default, an explicit style wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    initResources();

    m_controller.setTrackQuery(QString::fromUtf8(kTrack));
    m_engine.rootContext()->setContextProperty(QStringLiteral("lyricsController"), &m_controller);
    m_engine.rootContext()->setContextProperty(QStringLiteral("lyricSearch"), &m_search);

    // i18n() is not a global defined by importing org.kde.kirigami: it is a
    // method of the translator installed as the engine's root context object.
    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(&m_engine);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));

    QObject::connect(&m_engine, &QQmlApplicationEngine::warnings, [this](const QList<QQmlError> &warnings) {
        m_reported += warnings;
    });

    m_engine.load(QUrl(QStringLiteral("qrc:/qml/search.qml")));
}

void SearchQmlTest::loadsWithoutWarnings()
{
    for (const QQmlError &error : m_reported) {
        std::fprintf(stderr, "QML: %s\n", qPrintable(error.toString()));
    }
    QVERIFY2(rootObject() != nullptr, "search.qml creates a root object");
    QVERIFY2(m_reported.isEmpty(), "search.qml reports no QML errors or warnings");

    // An empty title would mean i18n() never resolved, in which case every label
    // in the window is blank too.
    QVERIFY2(!rootObject()->property("title").toString().isEmpty(), "the title resolved through i18n()");
    QVERIFY2(rootObject()->property("visible").toBool(), "the root window shows itself");
}

void SearchQmlTest::prefillsTheSearchFieldWithThePlayingTrack()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    // The field opens on what is playing, so the common case is Enter.
    QObject *field = byObjectName(QStringLiteral("queryField"));
    QVERIFY2(field != nullptr, "the search field is in the window");
    QVERIFY2(field->property("text").toString() == QString::fromUtf8(kTrack), "the field is prefilled with the track the controller reports");

    // The top line says the same thing, so the two never disagree, and there is
    // a track to pin a result to.
    QVERIFY2(labelText(QStringLiteral("trackLabel")) == QString::fromUtf8(kTrack), "the top line names the playing track");
    QVERIFY2(!flag("nothingPlaying"), "a track is playing");

    // Nothing is pinned yet, so no pin is offered at the top.
    QVERIFY2(!flag("hasPinnedLyric"), "no pinned lyric is offered while none is pinned");

    // With a query to send, the button is on; with nothing to search for it is
    // off rather than sending a request that could not answer anything.
    QObject *button = byObjectName(QStringLiteral("searchButton"));
    QVERIFY2(button != nullptr, "the search button is in the window");
    QVERIFY2(button->property("enabled").toBool(), "a prefilled query can be sent");
    m_controller.setTrackQuery(QString());
    QVERIFY2(field->property("text").toString().isEmpty(), "the field follows the track when it goes away");
    QVERIFY2(!button->property("enabled").toBool(), "an empty query cannot be sent");
    QVERIFY2(labelText(QStringLiteral("trackLabel")) == QLatin1String(kNothingPlaying), "with nothing playing the top line says so plainly");

    m_controller.setTrackQuery(QString::fromUtf8(kTrack));
    clearHostObjects();
}

void SearchQmlTest::mapsEachResultToItsRow()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    // Two sources, so a group boundary falls in the middle, and a result the
    // provider left fields out of.
    m_search.setResults(QVariantList{
        providerResult(QStringLiteral("netease"),
                       QStringLiteral("NetEase"),
                       QStringLiteral("12345"),
                       QStringLiteral("光の中へ"),
                       QStringLiteral("ヨルシカ"),
                       QStringLiteral("幻燈"),
                       225000),
        providerResult(QStringLiteral("lrclib"),
                       QStringLiteral("LRCLIB"),
                       QStringLiteral("999"),
                       QStringLiteral("光の中へ"),
                       QStringLiteral("Yorushika"),
                       QString(),
                       0),
        providerResult(QStringLiteral("lrclib"), QString(), QStringLiteral("7"), QStringLiteral("Walk"), QString(), QString(), 60000),
    });

    const QVariantList all = rows();
    QVERIFY2(all.size() == 3, "one row per provider result");

    const QVariantMap first = rowAt(0);
    QVERIFY2(first.value(QStringLiteral("index")).toInt() == 0, "the row knows the index it pins");
    QVERIFY2(first.value(QStringLiteral("provider")).toString() == QStringLiteral("netease"), "the row keeps the provider");
    QVERIFY2(first.value(QStringLiteral("providerLabel")).toString() == QStringLiteral("NetEase"), "the row shows the provider's own label");
    QVERIFY2(first.value(QStringLiteral("title")).toString() == QStringLiteral("光の中へ"), "the row shows the title");
    QVERIFY2(first.value(QStringLiteral("singer")).toString() == QStringLiteral("ヨルシカ"), "the row keeps the singer");
    QVERIFY2(first.value(QStringLiteral("album")).toString() == QStringLiteral("幻燈"), "the row keeps the album");
    QVERIFY2(first.value(QStringLiteral("details")).toString() == QStringLiteral("ヨルシカ · 幻燈"), "singer and album read as one line");
    QVERIFY2(first.value(QStringLiteral("lengthText")).toString() == QStringLiteral("3:45"), "225000 ms reads as 3:45");
    QVERIFY2(!first.value(QStringLiteral("groupStart")).toBool(), "the first row starts no new source group");

    const QVariantMap second = rowAt(1);
    QVERIFY2(second.value(QStringLiteral("index")).toInt() == 1, "the second row pins index 1");
    QVERIFY2(second.value(QStringLiteral("details")).toString() == QStringLiteral("Yorushika"), "a missing album leaves the singer alone");
    QVERIFY2(second.value(QStringLiteral("lengthText")).toString().isEmpty(), "a provider that gave no length shows none");
    QVERIFY2(second.value(QStringLiteral("groupStart")).toBool(), "the first result of a new source starts a group");

    const QVariantMap third = rowAt(2);
    QVERIFY2(third.value(QStringLiteral("index")).toInt() == 2, "the third row pins index 2");
    QVERIFY2(third.value(QStringLiteral("providerLabel")).toString() == QStringLiteral("lrclib"), "a missing label falls back to the provider");
    QVERIFY2(!third.value(QStringLiteral("groupStart")).toBool(), "a second result of the same source does not start a group");
    QVERIFY2(third.value(QStringLiteral("lengthText")).toString() == QStringLiteral("1:00"), "60000 ms reads as 1:00");
    QVERIFY2(third.value(QStringLiteral("details")).toString().isEmpty(), "a result with neither singer nor album has no detail line");

    // Fields a provider left out must not turn into the word "undefined".
    QVERIFY2(!third.value(QStringLiteral("title")).toString().contains(QStringLiteral("undefined")), "absent fields stay empty");

    m_search.setResults(QVariantList());
}

void SearchQmlTest::explainsWhyARowCannotBePinned()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    // A result with no song id carries no way back to its lyric, so its row must
    // say so in place of the button it cannot offer.
    m_search.setResults(QVariantList{
        providerResult(QStringLiteral("netease"),
                       QStringLiteral("NetEase"),
                       QStringLiteral("12345"),
                       QStringLiteral("光の中へ"),
                       QStringLiteral("ヨルシカ"),
                       QString(),
                       225000),
        providerResult(QStringLiteral("netease"), QStringLiteral("NetEase"), QString(), QStringLiteral("Walk"), QString(), QString(), 0),
    });

    QVERIFY2(pinnableRowCount() == 1, "only the result with an id can be pinned");
    QVERIFY2(rowAt(0).value(QStringLiteral("pinHint")).toString().isEmpty(), "a pinnable row carries no reason");
    QVERIFY2(rowAt(1).value(QStringLiteral("pinHint")).toString() == QLatin1String(kNoSongIdHint), "the row without an id says why it cannot be pinned");
    QVERIFY2(!rowAt(1).value(QStringLiteral("pinnable")).toBool(), "and it is not offered as pinnable");

    // With nothing playing there is no track to pin to at all, so every row says
    // so - in the words the top of the window already uses, from one place.
    m_controller.setTrackQuery(QString());
    QVERIFY2(flag("nothingPlaying"), "the window notices that nothing is playing");
    QVERIFY2(pinnableRowCount() == 0, "no row can be pinned while nothing is playing");
    QVERIFY2(pinHintText() == QLatin1String(kNothingPlaying), "every row gives the same reason in place of a button");

    m_controller.setTrackQuery(QString::fromUtf8(kTrack));
    clearHostObjects();
}

void SearchQmlTest::pinsAndForgetsThroughTheController()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    m_search.setResults(QVariantList{
        providerResult(QStringLiteral("netease"),
                       QStringLiteral("NetEase"),
                       QStringLiteral("12345"),
                       QStringLiteral("光の中へ"),
                       QStringLiteral("ヨルシカ"),
                       QString(),
                       225000),
        providerResult(QStringLiteral("lrclib"),
                       QStringLiteral("LRCLIB"),
                       QStringLiteral("999"),
                       QStringLiteral("光の中へ"),
                       QStringLiteral("Yorushika"),
                       QString(),
                       225000),
    });
    QVERIFY2(pinnableRowCount() == 2, "both rows can be pinned");

    // The button on a row pins that row. It calls pinResult() with the index the
    // row carries, which is the index of the provider list the controller
    // fetches from - so driving that helper is what the button does.
    QVERIFY2(QMetaObject::invokeMethod(rootObject(), "pinResult", Q_ARG(QVariant, 1)), "pinResult is callable from QML");
    QVERIFY2(m_controller.pinnedIndexes() == QList<int>{1}, "pinning reaches the controller with the row's index");
    QVERIFY2(QMetaObject::invokeMethod(rootObject(), "pinResult", Q_ARG(QVariant, 0)), "pinResult takes any row");
    QVERIFY2(m_controller.pinnedIndexes() == QList<int>({1, 0}), "each row pins its own index");

    // The pinned lyric is then announced at the top, with the way to drop it.
    m_controller.setPinnedDescription(QStringLiteral("NetEase — 光の中へ / ヨルシカ"));
    QVERIFY2(flag("hasPinnedLyric"), "the window notices the pin");
    QVERIFY2(labelText(QStringLiteral("pinnedLabel")) == QStringLiteral("Pinned for this song: NetEase — 光の中へ / ヨルシカ"),
             "the top says what is pinned and where it came from");

    QVERIFY2(QMetaObject::invokeMethod(rootObject(), "forgetPinnedLyric"), "forgetPinnedLyric is callable from QML");
    QVERIFY2(m_controller.forgetCount() == 1, "forgetting reaches the controller");

    clearHostObjects();
}

void SearchQmlTest::keepsTheDiagnosticsOutOfTheHeadline()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    // What the last pin or forget attempt came to is the controller's own
    // sentence, shown as an aside: dimmed, and never the same label as the state
    // the window is in.
    const QString pinStatus = QStringLiteral("That entry is no longer in the list.");
    m_controller.setPinStatus(pinStatus);
    QVERIFY2(flag("showPinStatus"), "a pin that could not be honoured is shown");
    QObject *pinStatusLabel = byObjectName(QStringLiteral("pinStatusLabel"));
    QVERIFY2(pinStatusLabel != nullptr, "the pin diagnostic has a label of its own");
    QVERIFY2(pinStatusLabel->property("text").toString() == pinStatus, "the pin diagnostic is the controller's own string");
    QVERIFY2(qFuzzyCompare(pinStatusLabel->property("opacity").toDouble(), 0.7), "the pin diagnostic is dimmed, so it does not read as the answer");
    QVERIFY2(pinStatusLabel != byObjectName(QStringLiteral("trackLabel")), "the pin diagnostic never takes the top line's place");
    QVERIFY2(pinStatusLabel != byObjectName(QStringLiteral("nothingFoundLabel")), "nor the place where the results are reported");

    QVERIFY2(QMetaObject::invokeMethod(rootObject(), "runSearch"), "runSearch is callable from QML");
    const QString searchFailure = QStringLiteral("network unreachable");
    m_search.setError(searchFailure);
    QVERIFY2(flag("showError"), "a failed search is shown");
    QObject *errorLabel = byObjectName(QStringLiteral("errorLabel"));
    QVERIFY2(errorLabel != nullptr, "the search failure has a label of its own");
    QVERIFY2(errorLabel->property("text").toString() == searchFailure, "the search failure is the provider's own string");
    QVERIFY2(qFuzzyCompare(errorLabel->property("opacity").toDouble(), 0.7), "the search failure is dimmed too");
    QVERIFY2(!flag("showNothingFound"), "a failure is not reported as an empty answer");

    // Nothing else on screen carries either diagnostic, which is what "never the
    // headline" means: they are asides, not the sentence the window is making.
    int pinStatusCopies = 0;
    int searchFailureCopies = 0;
    const auto children = rootObject()->findChildren<QObject *>();
    for (QObject *child : children) {
        // Matched on the string alone: it is distinctive, and every control that
        // shows text has a `text` property, whatever C++ class the style built it
        // from.
        const QString text = child->property("text").toString();
        pinStatusCopies += text == pinStatus ? 1 : 0;
        searchFailureCopies += text == searchFailure ? 1 : 0;
    }
    QVERIFY2(pinStatusCopies == 1, "the pin diagnostic appears once, as an aside");
    QVERIFY2(searchFailureCopies == 1, "the search failure appears once, as an aside");

    clearHostObjects();
}

void SearchQmlTest::walksTheResultsStates()
{
    QVERIFY(rootObject() != nullptr);
    clearHostObjects();

    // Before a search is asked for there is nothing to report, and the window
    // says how to start rather than that nothing was found.
    QVERIFY2(flag("showStartHint"), "an unsearched window invites a search");
    QVERIFY2(!flag("showNothingFound"), "an unsearched window is not a failed search");
    QVERIFY2(!flag("showBusy") && !flag("showError"), "neither in flight nor failed");
    QVERIFY2(labelText(QStringLiteral("startHintLabel")) == QLatin1String(kStartHint), "the invitation is the agreed sentence");

    // A search that is in flight says so.
    m_search.setBusy(true);
    QVERIFY2(flag("showBusy"), "a request in flight is announced");
    QVERIFY2(!flag("showStartHint") && !flag("showNothingFound"), "the invitation gives way to the search");
    QVERIFY2(labelText(QStringLiteral("busyLabel")) == QString::fromUtf8(kBusy), "the busy line is the agreed sentence");

    // Results that arrived while more are still coming stay on screen, so the
    // window is never blank between the first answer and the last.
    m_search.setResults(QVariantList{
        providerResult(QStringLiteral("netease"), QStringLiteral("NetEase"), QStringLiteral("1"), QStringLiteral("光の中へ"), QString(), QString(), 0)});
    QVERIFY2(rows().size() == 1, "results already in stay on screen while the search finishes");

    // The query the window sent is the field's own text, which is the track the
    // controller reports until the user types something else.
    QVERIFY2(QMetaObject::invokeMethod(rootObject(), "runSearch"), "runSearch is callable from QML");
    QVERIFY2(m_search.queries() == QStringList{QString::fromUtf8(kTrack)}, "the search sent the prefilled track query");
    QVERIFY2(!flag("showStartHint"), "a search has been asked for, so the invitation is gone");
    QVERIFY2(!flag("showNothingFound"), "results that are on screen are not an empty answer");

    // A search that came back with nothing says so plainly, in the whole
    // sentence, and only once one has been asked for.
    m_search.setResults(QVariantList());
    m_search.setBusy(false);
    QVERIFY2(!flag("showBusy"), "nothing is in flight");
    QVERIFY2(flag("showNothingFound"), "an empty answer is reported");
    QVERIFY2(labelText(QStringLiteral("nothingFoundLabel")) == QLatin1String(kNothingFound), "the empty line is the agreed sentence");

    clearHostObjects();
}

void SearchQmlTest::staysQuietWithoutTheHostObjects()
{
    // The window is also loaded where the host handed it nothing at all: no
    // controller, no search. It must still build, still show, and warn about
    // none of it - the guarded properties are what keeps an undeclared
    // identifier out of every binding.
    QQmlApplicationEngine bare;
    QList<QQmlError> reported;
    QObject::connect(&bare, &QQmlApplicationEngine::warnings, [&reported](const QList<QQmlError> &warnings) {
        reported += warnings;
    });

    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(&bare);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));

    bare.load(QUrl(QStringLiteral("qrc:/qml/search.qml")));

    for (const QQmlError &error : reported) {
        std::fprintf(stderr, "QML (bare): %s\n", qPrintable(error.toString()));
    }
    QVERIFY2(!bare.rootObjects().isEmpty(), "search.qml creates a root object without the host's objects");
    QVERIFY2(reported.isEmpty(), "search.qml reports no QML warning when the host's objects are absent");

    QObject *window = bare.rootObjects().constFirst();
    QVERIFY2(window->property("visible").toBool(), "the window still shows itself");
    QVERIFY2(window->property("rows").toList().isEmpty(), "there are no results to show");
    QVERIFY2(!window->property("showBusy").toBool() && !window->property("showError").toBool(), "nothing is claimed to be in flight or to have failed");
    QVERIFY2(!window->property("showPinStatus").toBool(), "and no pin is claimed to have been refused");
    QVERIFY2(window->property("showStartHint").toBool(), "the window still says how to start");
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("search_qml_test"));

    SearchQmlTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "search_qml_test.moc"
