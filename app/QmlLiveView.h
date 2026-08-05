#pragma once

#include <QTimer>
#include <QUrl>
#include <QWidget>

class LogosQmlBridge;
class QFileSystemWatcher;
class QQuickWidget;

// A QML view that re-creates itself when its source files change on disk, so
// `ui_qml` module authors can nudge a margin, save, and see the result without
// rebuilding or relaunching.
//
// Why re-create rather than just re-`setSource()` the same widget: QQmlEngine's
// type cache is per-engine, and clearComponentCache() only releases compilation
// units nothing still references. A live root object, a context property or a
// replica can keep the old unit alive, in which case the reload silently replays
// the previous QML — reporting success while changing nothing. Building a fresh
// QQmlEngine each time has no cache to defeat, so what is on disk is what runs.
//
// The backend is untouched by a reload: it lives in a separate ui-host process,
// and the bridge is re-bound to the new engine's root context.
//
// Enabled only when DEV_QML_PATH pointed this view at an editable source tree.
// Set LOGOS_QML_HOT_RELOAD=0 to opt out.
class QmlLiveView : public QWidget
{
    Q_OBJECT

public:
    // Whether a view at `baseDir` should hot reload (DEV_QML_PATH matched it).
    static bool isEnabledFor(const QString& baseDir);

    QmlLiveView(const QString& baseDir, const QString& qmlFile,
                LogosQmlBridge* bridge, QWidget* parent = nullptr);

    // False when the current QML failed to compile — the container stays alive
    // so the next save that compiles can bring the view back.
    bool hasView() const { return m_view != nullptr; }

public slots:
    void reload();

private:
    // Build and install a fresh QQuickWidget. Returns false on compile error.
    bool build();
    // (Re)arm watches over the whole tree, including files added since last time.
    void rescan();

    QString m_baseDir;
    QString m_qmlFile;
    LogosQmlBridge* m_bridge = nullptr;
    QQuickWidget* m_view = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer m_debounce;
};
