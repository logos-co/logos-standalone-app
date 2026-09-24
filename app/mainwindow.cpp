#include "mainwindow.h"

#include "UiPluginLoader.h"
#include "UiPluginPackage.h"
#include "logos_api.h"

#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QtQuickControls2/QQuickStyle>

MainWindow::MainWindow(logos::host::LogosCore& core,
                       const QString& pluginPath,
                       const QString& title,
                       int width,
                       int height,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_core(core)
{
    setWindowTitle(title.isEmpty() ? QFileInfo(pluginPath).baseName() : title);
    resize(width, height);
    // The style Basecamp sets, so plugins render the same in both hosts.
    QQuickStyle::setStyle("Basic");

    const QString resolvedPath = QFileInfo(pluginPath).absoluteFilePath();
    logos::ui::UiPluginRequest request;
    QString error;
    if (!logos::ui::resolveUiPluginPath(resolvedPath, &request, &error)) {
        qWarning().noquote() << error;
        showMessage(QStringLiteral("Failed to load UI plugin\n\n%1\n\n%2").arg(resolvedPath, error));
        return;
    }

    m_hostApi = new LogosAPI("standalone", this);
    // Runs on the loader's worker thread. A dependency's own optional
    // collaborators come up with it.
    m_loader = new logos::ui::UiPluginLoader(m_hostApi,
        [&core = m_core](const QString& name, bool) {
            return core.loadModule(name.toStdString(), LOGOS_LOAD_REQUIRED_AND_OPTIONAL);
        },
        this);
    m_loader->setAcceptInvokableWidgetFactories(true);

    connect(m_loader, &logos::ui::UiPluginLoader::pluginLoaded, this,
            [this, resolvedPath](const QString&, QWidget* widget) {
                setCentralWidget(widget);
                qInfo() << "Loaded UI plugin:" << resolvedPath;
            });
    connect(m_loader, &logos::ui::UiPluginLoader::pluginLoadFailed, this,
            [this, resolvedPath](const QString&, const QString& reason) {
                showMessage(QStringLiteral("Failed to load UI plugin\n\n%1\n\n%2")
                                .arg(resolvedPath, reason));
            });

    showMessage(QStringLiteral("Loading %1…").arg(request.name));
    m_loader->load(request);
}

void MainWindow::showMessage(const QString& text)
{
    auto* label = new QLabel(text, this);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    QFont font = label->font();
    font.setPointSize(13);
    label->setFont(font);
    setCentralWidget(label);
}
