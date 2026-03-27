#include "mainwindow.h"

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
#include <QtQuickControls2/QQuickStyle>

#include "logos_api.h"
#include "logos_api_client.h"

extern "C" {
    int logos_core_load_plugin(const char* plugin_name);
    int logos_core_load_plugin_with_dependencies(const char* plugin_name);
}

// Bridge exposed to QML as the "logos" context property.
// Mirrors LogosQmlBridge in logos-app.
class LogosBridge : public QObject
{
    Q_OBJECT
public:
    explicit LogosBridge(LogosAPI* api, QObject* parent = nullptr)
        : QObject(parent), m_api(api) {}

    Q_INVOKABLE QVariant callModule(const QString& moduleName,
                                    const QString& method,
                                    const QVariantList& args = {})
    {
        if (!m_api) return {};
        LogosAPIClient* client = m_api->getClient(moduleName);
        if (!client || !client->isConnected()) return {};
        return client->invokeRemoteMethod(moduleName, method, args);
    }

private:
    LogosAPI* m_api;
};

#include "mainwindow.moc"

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

void MainWindow::setupUi(const QString& pluginPath, int width, int height)
{
    QQuickStyle::setStyle("Fusion");
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
            if (plugin) {
                LogosAPI* logosAPI = new LogosAPI("standalone", this);
                bool ok = QMetaObject::invokeMethod(plugin, "createWidget",
                                                   Qt::DirectConnection,
                                                   Q_RETURN_ARG(QWidget*, widget),
                                                   Q_ARG(LogosAPI*, logosAPI));
                if (!ok || !widget) {
                    QMetaObject::invokeMethod(plugin, "createWidget",
                                             Qt::DirectConnection,
                                             Q_RETURN_ARG(QWidget*, widget));
                }
            }
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
            // QML plugin — "main" is a plain filename string in metadata.json.
            QString mainFile = pluginInfo.value("main").toString("Main.qml");

            LogosAPI* logosAPI = new LogosAPI("standalone", this);
            auto* bridge = new LogosBridge(logosAPI, this);

            auto* quickWidget = new QQuickWidget();
            quickWidget->setMinimumSize(800, 600);
            quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
            quickWidget->engine()->setBaseUrl(QUrl::fromLocalFile(resolvedPath + "/"));
            quickWidget->rootContext()->setContextProperty("logos", bridge);
            quickWidget->setSource(QUrl::fromLocalFile(resolvedPath + "/" + mainFile));

            if (quickWidget->status() == QQuickWidget::Error) {
                qWarning() << "Failed to load QML plugin:" << resolvedPath;
                for (const QQmlError& e : quickWidget->errors())
                    qWarning() << e.toString();
                delete quickWidget;
            } else {
                widget = quickWidget;
            }
        } else {
            // Dylib plugin — find the shared library file inside the directory.
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
                    if (plugin) {
                        // Heap-allocate LogosAPI parented to this so it outlives setupUi()
                        // and remains valid for the lifetime of any backend that stores it.
                        LogosAPI* logosAPI = new LogosAPI("standalone", this);
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
                    }
                }
            }
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
