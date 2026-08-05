#include "QmlLiveView.h"

#include "LogosQmlBridge.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QVBoxLayout>

namespace {
// Long enough to coalesce an editor's write burst (many save by truncating and
// rewriting, or by writing a temp file and renaming), short enough to feel live.
constexpr int kDebounceMs = 150;

// .js/.mjs count too — QML imports JS modules, and editing one must reload just
// like editing a .qml file.
const QStringList& qmlGlobs()
{
    static const QStringList globs{"*.qml", "*.js", "*.mjs"};
    return globs;
}
} // namespace

bool QmlLiveView::isEnabledFor(const QString& baseDir)
{
    if (qgetenv("LOGOS_QML_HOT_RELOAD") == "0") return false;

    const QString dev = QString::fromUtf8(qgetenv("DEV_QML_PATH")).trimmed();
    if (dev.isEmpty() || !QFileInfo(dev).isDir()) return false;

    // Only when the view actually resolved to the dev tree. If DEV_QML_PATH was
    // set but the entry file wasn't found there, mainwindow falls back to the
    // installed view, which lives in the read-only store — nothing to watch.
    const QString devCanonical = QDir(dev).canonicalPath();
    return !devCanonical.isEmpty()
        && devCanonical == QDir(baseDir).canonicalPath();
}

QmlLiveView::QmlLiveView(const QString& baseDir, const QString& qmlFile,
                         LogosQmlBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_baseDir(baseDir)
    , m_qmlFile(qmlFile)
    , m_bridge(bridge)
    , m_watcher(new QFileSystemWatcher(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &QmlLiveView::reload);

    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString&) { m_debounce.start(); });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { m_debounce.start(); });

    rescan();
    build();

    qInfo().noquote()
        << "QML hot reload active — watching" << m_baseDir
        << "\n  edit any .qml/.js under it and save; set LOGOS_QML_HOT_RELOAD=0 to disable";
}

void QmlLiveView::rescan()
{
    QStringList wanted{m_baseDir};

    QDirIterator dirs(m_baseDir, QDir::Dirs | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
    while (dirs.hasNext())
        wanted << dirs.next();

    QDirIterator files(m_baseDir, qmlGlobs(), QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext())
        wanted << files.next();

    // Re-arm from scratch instead of skipping paths already listed as watched:
    // a rename-save leaves the old inode (and its dead inotify watch) behind
    // while QFileSystemWatcher still reports the path as watched, which would
    // silently stop reloading after the first save.
    const QStringList previous = m_watcher->files() + m_watcher->directories();
    if (!previous.isEmpty())
        m_watcher->removePaths(previous);
    if (!wanted.isEmpty())
        m_watcher->addPaths(wanted);
}

bool QmlLiveView::build()
{
    auto* view = new QQuickWidget(this);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);

    QQmlEngine* engine = view->engine();
    engine->setBaseUrl(QUrl::fromLocalFile(m_baseDir + "/"));
    const QString entryDir = QFileInfo(m_qmlFile).absolutePath();
    if (!entryDir.isEmpty())
        engine->addImportPath(entryDir);

    // Re-bind the backend bridge: it outlives the engine, so a reload keeps the
    // module's C++ state and its ui-host process untouched.
    if (m_bridge)
        view->rootContext()->setContextProperty("logos", m_bridge);

    view->setSource(QUrl::fromLocalFile(m_qmlFile));

    if (view->status() == QQuickWidget::Error) {
        // Expected while mid-edit. Report and keep the container alive so the
        // next save that compiles restores the view.
        qWarning().noquote() << "QML error in" << m_qmlFile;
        const auto errors = view->errors();
        for (const QQmlError& error : errors)
            qWarning().noquote() << "   " << error.toString();
        view->deleteLater();
        return false;
    }

    layout()->addWidget(view);
    m_view = view;
    return true;
}

void QmlLiveView::reload()
{
    // Pick up files and folders added since the last pass, and replace watches
    // killed by rename-saves.
    rescan();

    // Drop the old view *and its engine*. deleteLater would keep the old engine
    // (and its cached compilation units) alive past the rebuild below.
    if (m_view) {
        layout()->removeWidget(m_view);
        delete m_view;
        m_view = nullptr;
    }

    if (build())
        qInfo().noquote() << "QML reloaded" << m_qmlFile;
}
