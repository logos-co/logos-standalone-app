#include "mainwindow.h"

#ifdef ENABLE_QML_INSPECTOR
#include "inspectorserver.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStyleHints>

// The core C API, through logos-cpp-sdk's wrapper rather than a prototype
// block of our own. This app used to hand-copy the declarations in TWO files,
// which is the shape that does not fail loudly: under C linkage the symbol
// mangles the same whatever the parameter types are, so a stale copy keeps
// compiling AND linking while passing the wrong thing. The mirror still
// exists — liblogos depends on logos-cpp-sdk, so that header cannot include
// liblogos' own — but now it exists ONCE, where it is tested.
#include "logos_host_core.h"

// Find and read metadata.json for a plugin path.
// For directories: looks inside the directory.
// For files: looks in the same directory, then the parent directory.
// Returns the parsed JSON object and sets pluginDir to the directory containing metadata.json.
static QJsonObject readPluginMetadata(const QString& pluginPath, QString& pluginDir)
{
    QFileInfo info(pluginPath);
    QString resolved = info.absoluteFilePath();
    QStringList candidates;

    if (info.isDir()) {
        candidates << resolved;
    } else {
        // For a file like result/lib/foo.dylib, check lib/ then result/
        QString dir = info.absolutePath();
        candidates << dir << QFileInfo(dir).absolutePath();
    }

    for (const QString& dir : candidates) {
        QFile f(dir + "/metadata.json");
        if (f.open(QIODevice::ReadOnly)) {
            pluginDir = dir;
            return QJsonDocument::fromJson(f.readAll()).object();
        }
    }
    return {};
}

int main(int argc, char* argv[])
{
    // Log to stderr by default.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING")) {
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    }

    QApplication app(argc, argv);
    app.setOrganizationName("Logos");
    app.setApplicationName("LogosStandalone");
    // macOS defaults Tab to text fields/lists only; Logos buttons/links need
    // all-controls Tab focus for keyboard navigation.
    app.styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Generic standalone Qt shell for loading and testing Logos UI plugins.\n\n"
        "Usage examples:\n"
        "  logos-standalone chat_ui.dylib\n"
        "  logos-standalone --plugin ./result/lib/accounts_ui.dylib\n"
        "  logos-standalone --plugin ./result/lib --modules-dir ./modules\n"
        "  logos-standalone --plugin chat_ui.so --modules-dir ./modules --load waku_module --load chat\n"
        "  nix run github:logos-co/logos-standalone-app -- ./result/lib/chat_ui.dylib"
    );
    parser.addHelpOption();

    QCommandLineOption pluginOption({"p", "plugin"},
        "Path to the UI plugin to load (.so / .dylib / .dll)", "path");
    QCommandLineOption modulesDirOption({"m", "modules-dir"},
        "Directory containing backend modules (default: ../modules relative to binary)", "dir");
    QCommandLineOption userDirOption({"u", "user-dir"},
        "Session data directory; isolates module state for this instance "
        "(default: the platform application data location)", "dir");
    QCommandLineOption loadOption({"l", "load"},
        "Backend module name to load before showing the UI; can be repeated", "module");
    QCommandLineOption titleOption({"t", "title"},
        "Window title (default: derived from plugin filename)", "title");
    QCommandLineOption widthOption("width",
        "Window width in pixels (default: 1024)", "px", "1024");
    QCommandLineOption heightOption("height",
        "Window height in pixels (default: 768)", "px", "768");

    parser.addOption(pluginOption);
    parser.addOption(modulesDirOption);
    parser.addOption(userDirOption);
    parser.addOption(loadOption);
    parser.addOption(titleOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addPositionalArgument("plugin", "UI plugin path (alternative to --plugin)");

    parser.process(app);

    // Resolve plugin path: --plugin takes priority, then first positional arg
    QString pluginPath;
    if (parser.isSet(pluginOption)) {
        pluginPath = parser.value(pluginOption);
    } else if (!parser.positionalArguments().isEmpty()) {
        pluginPath = parser.positionalArguments().first();
    }

    if (pluginPath.isEmpty()) {
        qCritical("Error: no UI plugin specified.");
        parser.showHelp(1);
    }

    // Resolve modules directory
    // Default: bundled modules dir alongside the binary (populated by nix build).
    // Users can still override with --modules-dir for additional / alternative modules.
    QString modulesDir;
    if (parser.isSet(modulesDirOption)) {
        modulesDir = QFileInfo(parser.value(modulesDirOption)).absoluteFilePath();
    } else {
        modulesDir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../modules");
    }

    // Resolve the session directory. Everything this run persists lives below
    // it, so two instances pointed at different directories keep their module
    // state apart. LOGOS_USER_DIR selects the same directory as --user-dir
    // (the flag wins), the way Logos Basecamp resolves its session directory.
    QString userDir = parser.isSet(userDirOption)
        ? parser.value(userDirOption)
        : qEnvironmentVariable("LOGOS_USER_DIR");
    if (!userDir.isEmpty()) {
        userDir = QFileInfo(userDir).absoluteFilePath();
        QFileInfo userDirInfo(userDir);
        if (userDirInfo.exists() && !userDirInfo.isDir()) {
            qCritical() << "Session directory exists but is not a directory:" << userDir;
            return 1;
        }
        if (!userDirInfo.exists() && !QDir().mkpath(userDir)) {
            qCritical() << "Failed to create session directory:" << userDir;
            return 1;
        }
    } else {
        userDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    qInfo() << "Session directory:" << userDir;

    // Setup logos core. Everything that must happen BEFORE start goes through
    // the config: the type folds that ordering into its constructor, so the
    // illegal sequence is unrepresentable rather than merely commented.
    logos::host::LogosCore::Config coreConfig;
    coreConfig.modulesDirs = { modulesDir.toStdString() };
    // Each module instance is handed <session dir>/module_data/<module>/<instance>
    // as its storage location.
    const QString moduleDataDir = userDir + "/module_data";
    coreConfig.persistenceBasePath = moduleDataDir.toStdString();

    // Destroyed at the end of main, AFTER app.exec() returns — the dtor is the
    // logos_core_cleanup() that used to sit there explicitly.
    logos::host::LogosCore core(argc, argv, std::move(coreConfig));
    core.start();
    qInfo() << "Logos Core started (modules dir:" << modulesDir << ")";


    // Load any additional modules requested via --load
    for (const QString& module : parser.values(loadOption)) {
        if (core.loadModule(module.toStdString(), LOGOS_LOAD_MODULE_ONLY)) {
            qInfo() << "Loaded module:" << module;
        } else {
            qWarning() << "Warning: failed to load module:" << module;
        }
    }

    // Read plugin metadata for title and icon
    QString metadataDir;
    QJsonObject metadata = readPluginMetadata(pluginPath, metadataDir);

    // Derive window title: --title flag > metadata "name" > plugin filename
    QString title;
    if (parser.isSet(titleOption)) {
        title = parser.value(titleOption);
    } else if (!metadata.isEmpty() && metadata.contains("name")) {
        title = metadata.value("name").toString();
    } else {
        title = QFileInfo(pluginPath).baseName();
    }

    // Set app icon from metadata "icon" field (relative file paths only)
    if (!metadata.isEmpty() && metadata.contains("icon")) {
        QString iconValue = metadata.value("icon").toString();
        if (!iconValue.isEmpty() && !iconValue.startsWith(":/")) {
            QString iconPath = metadataDir + "/" + iconValue;
            if (QFileInfo::exists(iconPath)) {
                app.setWindowIcon(QIcon(iconPath));
                qInfo() << "Set app icon from metadata:" << iconPath;
            } else {
                qInfo() << "Icon file not found:" << iconPath;
            }
        }
    }

    int width = parser.value(widthOption).toInt();
    int height = parser.value(heightOption).toInt();

    MainWindow window(core, pluginPath, title, width, height);
    window.show();

#ifdef ENABLE_QML_INSPECTOR
    // Start QML Inspector server (controlled by QML_INSPECTOR_PORT env var, default 3768)
    InspectorServer::attach(&window);
#endif

    return app.exec();
}
