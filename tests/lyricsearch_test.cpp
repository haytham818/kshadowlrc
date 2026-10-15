// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the manual-search fan-out: the provider order of the
// results, that an abandoned search's late answer is dropped, the busy flag,
// the error rules and the blank query. Driven by provider stubs; no network,
// no event loop needed beyond the one this test's main creates.

#include "enginestubs.h"

#include "lyrics/lyricsearch.h"

#include <QSignalSpy>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

using namespace kshadowlrc;

namespace
{

/*! A stub provider with distinct stable and display names, so a result row's
 *  \c provider and \c providerLabel can be told apart. */
class NamedProvider : public StubProvider
{
public:
    NamedProvider(const QString &name, const QString &label)
        : StubProvider(name)
        , m_name(name)
        , m_label(label)
    {
    }

    QString name() const override
    {
        return m_name;
    }
    QString displayName() const override
    {
        return m_label;
    }

private:
    QString m_name;
    QString m_label;
};

SongInfo makeSong(const QString &id, const QString &title)
{
    SongInfo song;
    song.id = id;
    song.title = title;
    song.singer = QStringLiteral("Some Singer");
    song.album = QStringLiteral("Some Album");
    song.lengthMs = 201000;
    return song;
}

QString providerOf(const QVariant &row)
{
    return row.toMap().value(QStringLiteral("provider")).toString();
}

} // namespace

class LyricSearchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resultsFollowProviderOrderNotArrival();
    void staleAnswerIsDropped();
    void allFailuresSetTheError();
    void emptyAnswerIsNotAnError();
    void busyRisesAndFalls();
    void blankQueryAsksNothingAndClears();
};

/*!
 * The one that matters: the later-answering provider is second in the list, so
 * its group must end up *below* the first provider's even though it answered
 * first. A naive append-on-arrival implementation fails this.
 */
void LyricSearchTest::resultsFollowProviderOrderNotArrival()
{
    NamedProvider first(QStringLiteral("first"), QStringLiteral("First Source"));
    first.autoDeliver = false;
    NamedProvider second(QStringLiteral("second"), QStringLiteral("Second Source"));
    second.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&first, &second});
    search.search(QStringLiteral("some song"));

    QCOMPARE(first.m_searchIds.size(), 1);
    QCOMPARE(second.m_searchIds.size(), 1);

    // The provider listed second answers first.
    second.deliverSearch(second.m_searchIds.at(0), {makeSong(QStringLiteral("s1"), QStringLiteral("Second Hit"))});
    QCOMPARE(search.results().size(), 1);
    QCOMPARE(providerOf(search.results().at(0)), QStringLiteral("second"));

    // Now the provider listed first answers: its group must move above.
    first.deliverSearch(first.m_searchIds.at(0), {makeSong(QStringLiteral("f1"), QStringLiteral("First Hit"))});
    QCOMPARE(search.results().size(), 2);

    const QVariantMap top = search.results().at(0).toMap();
    const QVariantMap bottom = search.results().at(1).toMap();
    QCOMPARE(top.value(QStringLiteral("provider")).toString(), QStringLiteral("first"));
    QCOMPARE(top.value(QStringLiteral("providerLabel")).toString(), QStringLiteral("First Source"));
    QCOMPARE(top.value(QStringLiteral("id")).toString(), QStringLiteral("f1"));
    QCOMPARE(top.value(QStringLiteral("title")).toString(), QStringLiteral("First Hit"));
    QCOMPARE(top.value(QStringLiteral("singer")).toString(), QStringLiteral("Some Singer"));
    QCOMPARE(top.value(QStringLiteral("album")).toString(), QStringLiteral("Some Album"));
    QCOMPARE(top.value(QStringLiteral("lengthMs")).toLongLong(), 201000);
    QCOMPARE(bottom.value(QStringLiteral("provider")).toString(), QStringLiteral("second"));
    QCOMPARE(bottom.value(QStringLiteral("id")).toString(), QStringLiteral("s1"));
}

/*! An answer for an abandoned search never lands and never re-arms busy. */
void LyricSearchTest::staleAnswerIsDropped()
{
    NamedProvider provider(QStringLiteral("p"), QStringLiteral("P"));
    provider.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&provider});

    search.search(QStringLiteral("old"));
    QCOMPARE(provider.m_searchIds.size(), 1);
    const quint64 oldId = provider.m_searchIds.at(0);

    search.search(QStringLiteral("new"));
    QCOMPARE(provider.m_searchIds.size(), 2);
    const quint64 newId = provider.m_searchIds.at(1);
    QVERIFY(search.busy());

    // The abandoned search's answer arrives late: nothing, and busy stays on
    // for the current generation.
    provider.deliverSearch(oldId, {makeSong(QStringLiteral("stale"), QStringLiteral("Stale"))});
    QVERIFY(search.results().isEmpty());
    QVERIFY(search.busy());

    provider.deliverSearch(newId, {makeSong(QStringLiteral("fresh"), QStringLiteral("Fresh"))});
    QCOMPARE(search.results().size(), 1);
    QCOMPARE(search.results().at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("fresh"));
    QVERIFY(!search.busy());
}

/*!
 * Only a total failure is an error, and it is the first failure in provider
 * order -- not the first to arrive, since the second provider reports first.
 */
void LyricSearchTest::allFailuresSetTheError()
{
    NamedProvider first(QStringLiteral("first"), QStringLiteral("First Source"));
    first.autoDeliver = false;
    NamedProvider second(QStringLiteral("second"), QStringLiteral("Second Source"));
    second.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&first, &second});
    search.search(QStringLiteral("q"));

    // The second provider fails first; one provider is still outstanding.
    second.failSearch(second.m_searchIds.at(0), QStringLiteral("second failed"));
    QVERIFY(search.error().isEmpty());

    first.failSearch(first.m_searchIds.at(0), QStringLiteral("first failed"));
    QCOMPARE(search.error(), QStringLiteral("first failed"));
    QVERIFY(!search.busy());
    QVERIFY(search.results().isEmpty());
}

/*! A provider answering with an empty list is a success, not a failure. */
void LyricSearchTest::emptyAnswerIsNotAnError()
{
    NamedProvider provider(QStringLiteral("p"), QStringLiteral("P"));
    provider.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&provider});
    search.search(QStringLiteral("q"));
    provider.deliverSearch(provider.m_searchIds.at(0), {});
    QVERIFY(search.error().isEmpty());
    QVERIFY(search.results().isEmpty());
    QVERIFY(!search.busy());

    // A failure beside an empty success is still not "all providers failed".
    NamedProvider failing(QStringLiteral("f"), QStringLiteral("F"));
    failing.autoDeliver = false;
    NamedProvider empty(QStringLiteral("e"), QStringLiteral("E"));
    empty.autoDeliver = false;

    LyricSearch mixed;
    mixed.setProviders({&failing, &empty});
    mixed.search(QStringLiteral("q"));
    failing.failSearch(failing.m_searchIds.at(0), QStringLiteral("boom"));
    empty.deliverSearch(empty.m_searchIds.at(0), {});
    QVERIFY(mixed.error().isEmpty());
    QVERIFY(!mixed.busy());
}

/*! busy rises for the search and falls only when the last provider answers. */
void LyricSearchTest::busyRisesAndFalls()
{
    NamedProvider first(QStringLiteral("first"), QStringLiteral("First Source"));
    first.autoDeliver = false;
    NamedProvider second(QStringLiteral("second"), QStringLiteral("Second Source"));
    second.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&first, &second});

    QSignalSpy spy(&search, &LyricSearch::busyChanged);
    QVERIFY(!search.busy());

    search.search(QStringLiteral("q"));
    QVERIFY(search.busy());

    first.deliverSearch(first.m_searchIds.at(0), {makeSong(QStringLiteral("f1"), QStringLiteral("First Hit"))});
    QVERIFY(search.busy()); // one provider still outstanding

    second.deliverSearch(second.m_searchIds.at(0), {});
    QVERIFY(!search.busy());
    QCOMPARE(spy.count(), 2); // true, then false
}

/*! A blank query asks nothing and clears results, error and busy. */
void LyricSearchTest::blankQueryAsksNothingAndClears()
{
    NamedProvider provider(QStringLiteral("p"), QStringLiteral("P"));
    provider.autoDeliver = false;

    LyricSearch search;
    search.setProviders({&provider});

    // A first search with a result to clear.
    search.search(QStringLiteral("q"));
    provider.deliverSearch(provider.m_searchIds.at(0), {makeSong(QStringLiteral("s1"), QStringLiteral("A Hit"))});
    QCOMPARE(search.results().size(), 1);

    search.search(QStringLiteral("  \t "));
    QVERIFY(search.results().isEmpty());
    QVERIFY(search.error().isEmpty());
    QVERIFY(!search.busy());
    QCOMPARE(provider.searchCalls, 1); // the blank query asked nothing
    QCOMPARE(provider.m_searchIds.size(), 1);

    // An error from an earlier search is cleared by the blank query too.
    search.search(QStringLiteral("q2"));
    provider.failSearch(provider.m_searchIds.at(1), QStringLiteral("boom"));
    QCOMPARE(search.error(), QStringLiteral("boom"));

    search.search(QString());
    QVERIFY(search.error().isEmpty());
    QCOMPARE(provider.searchCalls, 2); // still the two real searches only
}

QTEST_GUILESS_MAIN(LyricSearchTest)

#include "lyricsearch_test.moc"
