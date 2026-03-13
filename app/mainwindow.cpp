#include "mainwindow.h"

#include <QCoreApplication>
#include <QPluginLoader>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>

// LogosAPI for plugins that require it (IComponent interface)
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

    QPluginLoader loader(resolvedPath);
    QWidget* widget = nullptr;

    if (!loader.load()) {
        qWarning() << "Failed to load plugin from:" << resolvedPath;
        qWarning() << "Error:" << loader.errorString();
    } else {
        QObject* plugin = loader.instance();
        if (plugin) {
            // Prefer createWidget(LogosAPI*) — the canonical IComponent signature.
            // logos_core must already be running for LogosAPI to function.
            // Heap-allocate and parent to this so it outlives setupUi() and
            // remains valid for the lifetime of any plugin backend that stores it.
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

    if (widget) {
        setCentralWidget(widget);
        qInfo() << "Loaded UI plugin:" << resolvedPath;
    } else {
        qWarning() << "================================================";
        qWarning() << "Plugin loaded but createWidget() returned null";
        qWarning() << "Plugin path:" << resolvedPath;
        qWarning() << "Loader error:" << loader.errorString();
        qWarning() << "================================================";

        QWidget* fallback = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(fallback);

        QLabel* label = new QLabel(
            QString("Failed to load UI plugin\n\n%1\n\n%2")
                .arg(resolvedPath, loader.errorString()),
            fallback);
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
