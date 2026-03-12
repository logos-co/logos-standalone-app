#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFileInfo>
#include <iostream>

extern "C" {
    void logos_core_set_plugins_dir(const char* plugins_dir);
    void logos_core_add_plugins_dir(const char* plugins_dir);
    void logos_core_start();
    void logos_core_cleanup();
    char** logos_core_get_loaded_plugins();
    int logos_core_load_plugin(const char* plugin_name);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("Logos");
    app.setApplicationName("LogosStandalone");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Generic standalone Qt shell for loading and testing Logos UI plugins.\n\n"
        "Usage examples:\n"
        "  logos-standalone chat_ui.dylib\n"
        "  logos-standalone --plugin chat_ui.so --modules-dir ./modules --load capability_module --load waku_module --load chat\n"
        "  nix run github:logos-co/logos-standalone-app -- ./result/lib/chat_ui.dylib"
    );
    parser.addHelpOption();

    QCommandLineOption pluginOption({"p", "plugin"},
        "Path to the UI plugin to load (.so / .dylib / .dll)", "path");
    QCommandLineOption modulesDirOption({"m", "modules-dir"},
        "Directory containing backend modules (default: ../modules relative to binary)", "dir");
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
        std::cerr << "Error: no UI plugin specified.\n\n";
        parser.showHelp(1);
    }

    // Resolve modules directory
    QString modulesDir;
    if (parser.isSet(modulesDirOption)) {
        modulesDir = QFileInfo(parser.value(modulesDirOption)).absoluteFilePath();
    } else {
        modulesDir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../modules");
    }

    // Setup logos core
    logos_core_set_plugins_dir(modulesDir.toUtf8().constData());
    logos_core_start();
    std::cout << "Logos Core started (modules dir: " << modulesDir.toStdString() << ")" << std::endl;

    // Load requested backend modules in order
    for (const QString& module : parser.values(loadOption)) {
        if (logos_core_load_plugin(module.toUtf8().constData())) {
            std::cout << "Loaded module: " << module.toStdString() << std::endl;
        } else {
            std::cerr << "Warning: failed to load module: " << module.toStdString() << std::endl;
        }
    }

    // Derive window title from plugin filename if not explicitly set
    QString title = parser.isSet(titleOption)
        ? parser.value(titleOption)
        : QFileInfo(pluginPath).baseName();

    int width = parser.value(widthOption).toInt();
    int height = parser.value(heightOption).toInt();

    MainWindow window(pluginPath, title, width, height);
    window.show();

    int result = app.exec();
    logos_core_cleanup();
    return result;
}
