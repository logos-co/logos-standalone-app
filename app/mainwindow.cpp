#include "mainwindow.h"

#include <QPluginLoader>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>
#include <QQuickWidget>
#include <QQmlError>
#include <QUrl>
#include <QQmlEngine>

#include "logos_api.h"

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
    QString resolvedPath = QFileInfo(pluginPath).absoluteFilePath();
    QWidget* widget = nullptr;

    // All plugin types (ui and ui_qml) live in a directory.
    // Read manifest.json to decide how to load.
    QFile manifestFile(resolvedPath + "/manifest.json");
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "No manifest.json in plugin directory:" << resolvedPath;
    } else {
        QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        QString type = manifest.value("type").toString();

        if (type == "ui_qml") {
            // QML plugin — metadata.json carries a plain "main" filename.
            QFile metaFile(resolvedPath + "/metadata.json");
            QString mainFile = "Main.qml";
            if (metaFile.open(QIODevice::ReadOnly))
                mainFile = QJsonDocument::fromJson(metaFile.readAll()).object()
                               .value("main").toString(mainFile);

            auto* quickWidget = new QQuickWidget();
            quickWidget->setMinimumSize(800, 600);
            quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
            quickWidget->engine()->setBaseUrl(QUrl::fromLocalFile(resolvedPath + "/"));
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
