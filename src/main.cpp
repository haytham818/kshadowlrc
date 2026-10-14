// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "app/application.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kshadowlrc"));

    KAboutData aboutData = kshadowlrc::Application::makeAboutData();
    // QCommandLineParser builds --version's output and its error prefix from
    // these; set them directly because no QApplication exists yet.
    QCoreApplication::setApplicationName(aboutData.componentName());
    QCoreApplication::setApplicationVersion(aboutData.version());

    QCommandLineParser parser;
    const QCommandLineOption versionOption = parser.addVersionOption();
    // Which output the overlay attaches to. Overrides the settings file's
    // outputName; absent/empty means the settings value, itself empty by
    // default (the primary output). An unknown name falls back to the primary.
    const QCommandLineOption outputOption(QStringLiteral("output"),
                                          i18n("Output to place the overlay on, e.g. HDMI-A-1. Overrides the configured output; "
                                               "empty means the primary output."),
                                          QStringLiteral("name"));
    parser.addOption(outputOption);
    // Run the built-in demo lyric instead of following media players. For
    // development and for exercising the overlay with no player around.
    const QCommandLineOption demoOption(QStringLiteral("demo"), i18n("Play the built-in demo lyric instead of following the desktop's media players."));
    parser.addOption(demoOption);
    aboutData.setupCommandLine(&parser);

    // Pass 1, before QApplication: its constructor aborts without a display, yet
    // --help/--version/--author/--license must work headless and before the
    // single-instance hand-off. Read Qt's own options as words so e.g. -reverse
    // is not mistaken for -v.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    (void)parser.parse(arguments); // unknown options are reported by pass 2
    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp();
    }
    if (parser.isSet(versionOption)) {
        parser.showVersion();
    }
    if (parser.isSet(QStringLiteral("author")) || parser.isSet(QStringLiteral("license"))) {
        aboutData.processCommandLine(&parser);
    }

    kshadowlrc::Application app(argc, argv);
    // Pass 2: QApplication removed its own options; anything left that the parser
    // does not know is an error.
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);
    parser.process(app);
    aboutData.processCommandLine(&parser); // applies --desktopfile

    return app.run(aboutData, parser.value(outputOption), parser.isSet(demoOption));
}
