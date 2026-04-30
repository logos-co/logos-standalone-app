#include "mainwindow.h"
#include "LogosQmlBridge.h"
#include "ViewModuleHost.h"

#include <QPluginLoader>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>
#include <QQuickWidget>
#include <QQmlError>
#include <QUrl>
#include <QQmlEngine>
#include <QQmlContext>
#include <QEventLoop>
#include <QTimer>
#include <QtQuickControls2/QQuickStyle>

#include "logos_api.h"

extern "C" {
    int logos_core_load_plugin(const char* plugin_name);
    int logos_core_load_plugin_with_dependencies(const char* plugin_name);
}

MainWindow::MainWindow(const QString& pluginPath,
                       const QString& title,
                       int width,
                       int height,
                       QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(title.isEmpty() ? QFileInfo(pluginPath).baseName() : title);
    setupUi(pluginPath, width, height);
}

QWidget* MainWindow::loadQmlView(const QString& baseDir, const QString& qmlFile, LogosQmlBridge* bridge)
{
    auto* quickWidget = new QQuickWidget();
    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quickWidget->engine()->setBaseUrl(QUrl::fromLocalFile(baseDir + "/"));
    const QString qmlEntryDir = QFileInfo(qmlFile).absolutePath();
    if (!qmlEntryDir.isEmpty())
        quickWidget->engine()->addImportPath(qmlEntryDir);
    quickWidget->rootContext()->setContextProperty("logos", bridge);
    quickWidget->setSource(QUrl::fromLocalFile(qmlFile));

    if (quickWidget->status() == QQuickWidget::Error) {
        qWarning() << "Failed to load QML:" << qmlFile;
        for (const QQmlError& e : quickWidget->errors())
            qWarning() << e.toString();
        delete quickWidget;
        return nullptr;
    }
    return quickWidget;
}

QWidget* MainWindow::loadLegacyWidget(QObject* plugin)
{
    LogosAPI* logosAPI = new LogosAPI("standalone", this);
    QWidget* widget = nullptr;
    bool ok = QMetaObject::invokeMethod(plugin, "createWidget",
                                        Qt::DirectConnection,
                                        Q_RETURN_ARG(QWidget*, widget),
                                        Q_ARG(LogosAPI*, logosAPI));
    // Fallback: some plugins expose createWidget() with no args
    if (!ok || !widget) {
        QMetaObject::invokeMethod(plugin, "createWidget",
                                  Qt::DirectConnection,
                                  Q_RETURN_ARG(QWidget*, widget));
    }
    return widget;
}

void MainWindow::setupUi(const QString& pluginPath, int width, int height)
{
    // Align with logos-basecamp (which sets "Basic") so UI plugins render
    // identically in both hosts. The Logos design system assumes a minimal
    // style substrate; Fusion's opinionated defaults (focus rings, palette
    // pulls, built-in indicator images) fight against the theme.
    QQuickStyle::setStyle("Basic");
    QString resolvedPath = QFileInfo(pluginPath).absoluteFilePath();
    QWidget* widget = nullptr;

    QFileInfo pathInfo(resolvedPath);

    if (pathInfo.isFile()) {
        // Raw dylib/so/dll passed directly — load it without requiring a metadata file.
        QPluginLoader loader(resolvedPath);
        if (!loader.load()) {
            qWarning() << "Failed to load plugin:" << loader.errorString();
        } else {
            QObject* plugin = loader.instance();
            if (plugin)
                widget = loadLegacyWidget(plugin);
        }
    } else {
    // Package directory path — look for metadata.json / manifest.json.
    // Prefer metadata.json (plain format used by individual plugin repos);
    // fall back to manifest.json (platform-map format used in the standalone plugins/ dir).
    QJsonObject pluginInfo;
    for (const QString& name : {QString("metadata.json"), QString("manifest.json")}) {
        QFile f(resolvedPath + "/" + name);
        if (f.open(QIODevice::ReadOnly)) {
            pluginInfo = QJsonDocument::fromJson(f.readAll()).object();
            break;
        }
    }
    if (pluginInfo.isEmpty()) {
        qWarning() << "No metadata.json or manifest.json in plugin directory:" << resolvedPath;
    } else {
        QString type = pluginInfo.value("type").toString();

        // Load backend dependencies declared in metadata before showing the UI,
        // mirroring logos-app's MainUIBackend::loadUiModule() dependency handling.
        // Uses logos_core_load_plugin_with_dependencies() to automatically resolve
        // and load transitive dependencies in the correct order.
        for (const QJsonValue& dep : pluginInfo.value("dependencies").toArray()) {
            QString depName = dep.toString();
            if (depName.isEmpty()) continue;
            if (logos_core_load_plugin_with_dependencies(depName.toUtf8().constData())) {
                qInfo() << "Loaded dependency (with transitive deps):" << depName;
            } else {
                qWarning() << "Failed to load dependency:" << depName;
            }
        }

        if (type == "ui_qml") {
            // ui_qml contract: "view" (required) = QML entry point;
            // "main" (optional) = backend Qt plugin lib. If a backend lib is
            // shipped alongside, run it in an isolated ui-host process and
            // bridge to the QML view; otherwise load the QML directly.
            QString viewField = pluginInfo.value("view").toString();
            QString qmlViewPath;
            QString qmlBaseDir = resolvedPath;
            QString pluginSoPath;
            if (viewField.isEmpty()) {
                qWarning() << "ui_qml module missing required 'view' field:" << resolvedPath;
            } else {
                // Discover a backend plugin library, if any, in the install dir.
                QStringList libs = QDir(resolvedPath).entryList(
                    {"*.dylib", "*.so", "*.dll"}, QDir::Files);
                if (!libs.isEmpty()) {
                    pluginSoPath = resolvedPath + "/" + libs.first();
                }
                qmlViewPath = resolvedPath + "/" + viewField;

                // DEV_QML_PATH: load QML from a source directory instead of the
                // installed one, so edits can be picked up by relaunching without
                // a rebuild. The env var should point at the directory holding
                // the view entry file (e.g. .../src/qml).
                const QString devQmlPath = QString::fromUtf8(qgetenv("DEV_QML_PATH")).trimmed();
                if (!devQmlPath.isEmpty()) {
                    if (QFileInfo(devQmlPath).isDir()) {
                        const QString entry = QFileInfo(viewField).fileName();
                        const QString override = QDir(devQmlPath).absoluteFilePath(entry);
                        if (QFile::exists(override)) {
                            qInfo().noquote() << "DEV_QML_PATH override active:" << override;
                            qmlViewPath = override;
                            qmlBaseDir = devQmlPath;
                        } else {
                            qWarning().noquote() << "DEV_QML_PATH set but entry not found:"
                                                 << override << "- using installed view";
                        }
                    } else {
                        qWarning().noquote() << "DEV_QML_PATH is not a directory:" << devQmlPath
                                             << "- using installed view";
                    }
                }
            }

            QString moduleName = pluginInfo.value("name").toString();
            if (moduleName.isEmpty()) {
                moduleName = QFileInfo(resolvedPath).baseName();
                qWarning() << "View module metadata missing 'name'; defaulting to" << moduleName;
            }

            if (!QFile::exists(qmlViewPath)) {
                qWarning() << "View module QML file not found:" << qmlViewPath;
            } else if (pluginSoPath.isEmpty()) {
                // QML-only path: no backend, load QML directly in-process.
                LogosAPI* logosAPI = new LogosAPI("standalone", this);
                auto* bridge = new LogosQmlBridge(logosAPI, this);
                widget = loadQmlView(qmlBaseDir, qmlViewPath, bridge);
            } else {
                // Backend present: spawn isolated ui-host and bridge to it.
                auto* viewHost = new ViewModuleHost(this);
                bool spawned = viewHost->spawn(moduleName, pluginSoPath);
                if (!spawned) {
                    qWarning() << "Failed to spawn ui-host for view module" << moduleName;
                    delete viewHost;
                } else {
                    // Wait for ready signal
                    QEventLoop waitLoop;
                    bool hostReady = false;
                    QTimer timeout;
                    timeout.setSingleShot(true);
                    connect(viewHost, &ViewModuleHost::ready, &waitLoop, [&]() {
                        hostReady = true;
                        waitLoop.quit();
                    });
                    connect(&timeout, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
                    timeout.start(10000);
                    waitLoop.exec();

                    if (!hostReady) {
                        qWarning() << "Timeout waiting for ui-host ready for" << moduleName;
                        viewHost->stop();
                        delete viewHost;
                    } else {
                        LogosAPI* logosAPI = new LogosAPI("standalone", this);
                        auto* bridge = new LogosQmlBridge(logosAPI, this);
                        bridge->setViewModuleSocket(moduleName, viewHost->socketName());

                        // By convention each view module ships a
                        // typed replica factory plugin alongside its
                        // backend plugin, named
                        // "<moduleName>_replica_factory.{so,dylib}".
                        // If present, register it with the bridge so
                        // logos.module("<moduleName>") in QML returns
                        // a statically-typed replica.
                        for (const QString& suffix : { QStringLiteral(".dylib"),
                                                       QStringLiteral(".so") }) {
                            QString factoryPath = resolvedPath + "/"
                                + moduleName + "_replica_factory" + suffix;
                            if (QFile::exists(factoryPath)) {
                                bridge->setViewReplicaPlugin(moduleName, factoryPath);
                                break;
                            }
                        }

                        widget = loadQmlView(qmlBaseDir, qmlViewPath, bridge);
                        if (!widget) {
                            viewHost->stop();
                            delete viewHost;
                        }
                    }
                }
            }
        } else if (type == "ui") {
            // Legacy dylib plugin (pure C++ IComponent, no QML view)
            QStringList libs = QDir(resolvedPath).entryList(
                {"*.dylib", "*.so", "*.dll"}, QDir::Files);
            if (libs.isEmpty()) {
                qWarning() << "No shared library found in plugin directory:" << resolvedPath;
            } else {
                QString dylibPath = resolvedPath + "/" + libs.first();
                QPluginLoader loader(dylibPath);
                if (!loader.load()) {
                    qWarning() << "Failed to load plugin:" << loader.errorString();
                } else {
                    QObject* plugin = loader.instance();
                    if (plugin)
                        widget = loadLegacyWidget(plugin);
                }
            }
        } else {
            qWarning() << "Unknown plugin type:" << type << "in" << resolvedPath;
        }
    }
    }

    if (widget) {
        setCentralWidget(widget);
        qInfo() << "Loaded UI plugin:" << resolvedPath;
    } else {
        QWidget* fallback = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(fallback);
        QLabel* label = new QLabel(
            QString("Failed to load UI plugin\n\n%1").arg(resolvedPath), fallback);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        QFont font = label->font();
        font.setPointSize(13);
        label->setFont(font);
        layout->addWidget(label);
        setCentralWidget(fallback);
    }

    resize(width, height);
}
