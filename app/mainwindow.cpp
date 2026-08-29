#include "mainwindow.h"
#include "LogosQmlBridge.h"
#include "QmlLiveView.h"
#include "ViewModuleHost.h"

#include <QPluginLoader>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
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
#include "logos_consumer.h"

extern "C" {
    int logos_core_load_module(const char* module_name, bool with_dependencies);
}

namespace {

/// The backend library a plugin DECLARES, or nothing.
///
/// This used to glob `*.dylib/*.so/*.dll` and take `libs.first()` — the
/// alphabetically first file, since QDir sorts by name. A plugin that ships any
/// other library alongside its own (an external dependency, a replica factory)
/// could therefore hand ui-host a library that is not a Qt plugin at all, and
/// the view would never render. `signer_ui` ships two and worked only because
/// `signer_ui_plugin` sorts before `signer_ui_replica_factory`.
///
/// Mirrors lgpm's resolveMainFilePath(), which is what Basecamp already gets its
/// path from. Returns empty when nothing is declared — for `ui_qml` that is a
/// legitimate QML-only plugin, and for anything else it is a malformed one. We
/// do not guess either way.
QString resolveBackendLib(const QString& dir,
                          const QJsonObject& metadata,
                          const QJsonObject& manifest)
{
    const QString variant = [&] {
        QFile f(dir + "/variant");
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll()).trimmed();
    }();

    // 1. manifest.json — a real filename. An object is keyed by variant; prefer
    // the variant actually installed here, then any listed one present on disk.
    const QJsonValue manifestMain = manifest.value("main");
    if (manifestMain.isObject()) {
        const QJsonObject byVariant = manifestMain.toObject();
        QStringList keys;
        if (!variant.isEmpty() && byVariant.contains(variant)) keys << variant;
        for (const QString& k : byVariant.keys())
            if (k != variant) keys << k;
        for (const QString& k : keys) {
            const QString file = byVariant.value(k).toString();
            if (file.isEmpty()) continue;
            const QString path = dir + "/" + file;
            if (QFile::exists(path)) return path;
        }
        return QString();
    }
    if (manifestMain.isString() && !manifestMain.toString().isEmpty()) {
        const QString path = dir + "/" + manifestMain.toString();
        return QFile::exists(path) ? path : QString();
    }

    // 2. metadata.json — a LOGICAL name ("signer_ui_plugin"), carrying neither
    // the platform prefix nor the suffix, so it has to be spelled out.
    const QString name = metadata.value("main").toString().trimmed();
    if (name.isEmpty()) return QString();
    for (const QString& candidate : {
#if defined(Q_OS_WIN)
             name + ".dll", "lib" + name + ".dll",
#elif defined(Q_OS_MAC)
             name + ".dylib", "lib" + name + ".dylib",
#else
             "lib" + name + ".so", name + ".so",
#endif
         }) {
        const QString path = dir + "/" + candidate;
        if (QFile::exists(path)) return path;
    }
    return QString();
}

} // namespace

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
    if (QmlLiveView::isEnabledFor(baseDir))
        return new QmlLiveView(baseDir, qmlFile, bridge);

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

LogosAPI* MainWindow::hostApi()
{
    if (!m_hostApi)
        m_hostApi = new LogosAPI("standalone", this);
    return m_hostApi;
}

logos::ConsumerIdentity MainWindow::consumerFor(const QString& name)
{
    if (name.isEmpty()) {
        qWarning() << "refusing to build an identity for an unnamed plugin";
        return {};
    }
    auto it = m_consumers.constFind(name);
    if (it != m_consumers.constEnd())
        return it.value();

    // Isolate, construct, mint, register, adopt — all of it, in the one order
    // that leaves no window, and over the HOST's client because
    // informModuleToken is accepted only from the trusted core/capability
    // channel. See logos_consumer.h.
    logos::ConsumerIdentity consumer = logos::admitConsumer(name, hostApi(), this);
    if (!consumer) {
        qWarning() << "could not admit" << name << "as a consumer -"
                   << "refusing to run it with the host's authority";
        return {};
    }
    m_consumers.insert(name, consumer);

    return consumer;
}

QWidget* MainWindow::loadLegacyWidget(QObject* plugin, const QString& identity)
{
    // The plugin's own identity, not the host's: a legacy widget plugin calls
    // modules through exactly the LogosAPI it is handed here. One call where
    // there were two — and the credential it mints now reaches the plugin's
    // store instead of being registered and dropped.
    const logos::ConsumerIdentity consumer = consumerFor(identity);
    if (!consumer) {
        qWarning() << "not loading legacy plugin" << identity
                   << "- it could not be admitted as a consumer";
        return nullptr;
    }
    LogosAPI* logosAPI = consumer.api;

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
                widget = loadLegacyWidget(plugin, pathInfo.baseName());
        }
    } else {
    // Package directory path — look for metadata.json / manifest.json.
    // Prefer metadata.json (plain format used by individual plugin repos);
    // fall back to manifest.json (platform-map format used in the standalone plugins/ dir).
    //
    // BOTH are read, not just the first found: the two disagree about what
    // `main` means. metadata.json carries a logical name ("signer_ui_plugin"),
    // manifest.json the actual filename keyed by variant. resolveBackendLib()
    // needs whichever is present, and the installed tree ships both.
    auto readJson = [&](const QString& name) {
        QFile f(resolvedPath + "/" + name);
        return f.open(QIODevice::ReadOnly)
                   ? QJsonDocument::fromJson(f.readAll()).object()
                   : QJsonObject();
    };
    const QJsonObject metadataJson = readJson(QStringLiteral("metadata.json"));
    const QJsonObject manifestJson = readJson(QStringLiteral("manifest.json"));
    QJsonObject pluginInfo = metadataJson.isEmpty() ? manifestJson : metadataJson;
    if (pluginInfo.isEmpty()) {
        qWarning() << "No metadata.json or manifest.json in plugin directory:" << resolvedPath;
    } else {
        QString type = pluginInfo.value("type").toString();

        // Load backend dependencies declared in metadata before showing the UI,
        // mirroring logos-app's MainUIBackend::loadUiModule() dependency handling.
        // Uses logos_core_load_module(name, true) to automatically resolve
        // and load transitive dependencies in the correct order. An entry is
        // either a bare name or an object holding that name alongside the
        // constraints an installer resolves it by.
        for (const QJsonValue& dep : pluginInfo.value("dependencies").toArray()) {
            QString depName = dep.isObject() ? dep.toObject().value("name").toString()
                                             : dep.toString();
            if (depName.isEmpty()) continue;
            if (logos_core_load_module(depName.toUtf8().constData(), true)) {
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
                // The backend the plugin DECLARES. Empty is normal here: a
                // QML-only ui_qml plugin ships no backend at all.
                pluginSoPath = resolveBackendLib(resolvedPath, metadataJson, manifestJson);
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
                //
                // This branch is the one that used to run entirely on the
                // host's identity: it built a "standalone" LogosAPI (host
                // ambient ring — every loaded module's root token) and never
                // registered the module with capability_module at all, because
                // registration was tangled up with spawning a ui-host. The QML
                // could therefore reach any module in the process with no
                // handshake. Both halves are now unconditional.
                const logos::ConsumerIdentity consumer = consumerFor(moduleName);
                if (!consumer) {
                    qWarning() << "not loading QML-only view module" << moduleName
                               << "- it could not be admitted as a consumer";
                } else {
                    auto* bridge = new LogosQmlBridge(consumer.api, this);
                    widget = loadQmlView(qmlBaseDir, qmlViewPath, bridge);
                }
            } else {
                // ONE admission, and it happens BEFORE ui-host is spawned.
                // ui-host runs the plugin's initLogos synchronously, so a
                // backend ctor may fire its first (token-gated)
                // capability_module.requestModule before ViewModuleHost emits
                // ready(); registering only after ready races those calls, which
                // then reach capability_module's fail-closed gate before the
                // credential is known and are rejected as unauthorized.
                // admitConsumer's registration is synchronous, so that race is
                // closed here rather than merely narrowed.
                //
                // The order used to be inverted in this file — registration
                // first, store second — which registered a credential for an
                // identity whose store did not yet exist.
                const logos::ConsumerIdentity consumer = consumerFor(moduleName);
                LogosAPI* logosAPI = consumer.api;

                // Fall through to the "no widget" fallback rather than
                // returning: setupUi still has to put something in the window.
                auto* viewHost = consumer ? new ViewModuleHost(this) : nullptr;
                bool spawned = viewHost
                    && viewHost->spawn(moduleName, pluginSoPath, consumer.credential);
                if (!spawned) {
                    qWarning() << (consumer
                        ? "Failed to spawn ui-host for view module"
                        : "not loading view module (it could not be admitted)")
                        << moduleName;
                    delete viewHost;
                    // The LogosAPI is cached in m_consumers and parented to this
                    // window; deleting it here would leave a dangling entry
                    // that the next load of the same module would hand out.
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
                        // logosAPI stays: it is owned by m_consumers/this.
                    } else {
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
            const QString dylibPath =
                resolveBackendLib(resolvedPath, metadataJson, manifestJson);
            if (dylibPath.isEmpty()) {
                // A legacy `ui` plugin IS its library, so an undeclared one is
                // malformed. Refuse rather than guess at a file in the directory.
                qWarning() << "Plugin declares no resolvable 'main' library:" << resolvedPath;
            } else {
                QPluginLoader loader(dylibPath);
                if (!loader.load()) {
                    qWarning() << "Failed to load plugin:" << loader.errorString();
                } else {
                    QObject* plugin = loader.instance();
                    if (plugin) {
                        QString legacyName = pluginInfo.value("name").toString();
                        if (legacyName.isEmpty())
                            legacyName = QFileInfo(resolvedPath).baseName();
                        widget = loadLegacyWidget(plugin, legacyName);
                    }
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
