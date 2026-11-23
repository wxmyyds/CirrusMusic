#pragma once
#include <QLoggingCategory>

// Every category of the app (docs/tasks/logging.md §4.1), declared here only, so that
// any layer can log without defining its own. A new module adds its category here.
Q_DECLARE_LOGGING_CATEGORY(lcApp)       // cirrus.app: start, exit, single instance, GPU
Q_DECLARE_LOGGING_CATEGORY(lcLog)       // cirrus.log: the logging system itself
Q_DECLARE_LOGGING_CATEGORY(lcNet)       // cirrus.net: HTTP transport
Q_DECLARE_LOGGING_CATEGORY(lcApi)       // cirrus.api: NetEase operations, codes, mapping
Q_DECLARE_LOGGING_CATEGORY(lcAuth)      // cirrus.auth: guest bootstrap, sign-in, sessions
Q_DECLARE_LOGGING_CATEGORY(lcSecrets)   // cirrus.secrets: keyring results, never contents
Q_DECLARE_LOGGING_CATEGORY(lcStorage)   // cirrus.storage: SQLite
Q_DECLARE_LOGGING_CATEGORY(lcCache)     // cirrus.cache: covers, lyrics and other resources
Q_DECLARE_LOGGING_CATEGORY(lcPlayback)  // cirrus.playback: session, queue, loads
Q_DECLARE_LOGGING_CATEGORY(lcMedia)     // cirrus.media: audio engine and audio store
Q_DECLARE_LOGGING_CATEGORY(lcReport)    // cirrus.report: listening reports
Q_DECLARE_LOGGING_CATEGORY(lcTogether)  // cirrus.together: listen-together rooms and sync
Q_DECLARE_LOGGING_CATEGORY(lcPlatform)  // cirrus.platform: MPRIS, SMTC, Now Playing, tray
Q_DECLARE_LOGGING_CATEGORY(lcUi)        // cirrus.ui: presentation layer, models
Q_DECLARE_LOGGING_CATEGORY(lcUiBackend) // cirrus.ui.backend: the backend host
Q_DECLARE_LOGGING_CATEGORY(lcUiTheme)   // cirrus.ui.theme: fonts, palettes, covers
Q_DECLARE_LOGGING_CATEGORY(lcUiQml)     // cirrus.ui.qml: the QML engine's warnings
Q_DECLARE_LOGGING_CATEGORY(lcUiNav)     // cirrus.ui.nav: pages, dialogs, notices, marks
