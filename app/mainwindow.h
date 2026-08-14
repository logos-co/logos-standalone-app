#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

class LogosAPI;
class LogosQmlBridge;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& pluginPath,
                        const QString& title = QString(),
                        int width = 1024,
                        int height = 768,
                        QWidget* parent = nullptr);
    ~MainWindow() = default;

private:
    void setupUi(const QString& pluginPath, int width, int height);
    QWidget* loadQmlView(const QString& baseDir, const QString& qmlFile, LogosQmlBridge* bridge);
    QWidget* loadLegacyWidget(QObject* plugin, const QString& identity);

    // ── identities ──────────────────────────────────────────────────────
    //
    // The host's own channel. "standalone" is this process speaking as itself:
    // it is the trusted core/capability channel informModuleToken requires, and
    // it keeps the host's ambient token ring. Only the host uses it.
    LogosAPI* hostApi();

    // The plugin's channel. Bound to an ISOLATED token store, so the plugin
    // starts with the bootstrap tokens only instead of inheriting the host's
    // ring — which held every loaded module's root token and made every plugin
    // able to call every module without a capability handshake.
    //
    // nullptr means the identity could not be isolated; that is fatal for the
    // plugin rather than a cue to fall back to hostApi().
    LogosAPI* apiForPlugin(const QString& name);

    // Make `name` a known caller at capability_module. Without it the isolated
    // identity's first requestModule is refused and it can obtain no token.
    void registerPluginIdentity(const QString& name, const QString& authToken);

    LogosAPI* m_hostApi = nullptr;
    QHash<QString, LogosAPI*> m_pluginApis;
};
