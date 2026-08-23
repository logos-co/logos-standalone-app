#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

// logos::ConsumerIdentity — what logos::admitConsumer hands back.
#include "logos_consumer.h"

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

    // The plugin's channel. logos::admitConsumer gives it an ISOLATED token
    // store, mints a credential, registers that credential with
    // capability_module over hostApi()'s trusted channel, and installs it — in
    // that order. Without the store the plugin inherits the host's ring, which
    // held every loaded module's root token and made every plugin able to call
    // every module with no capability handshake; without the credential it
    // holds nothing it is entitled to and can call nothing.
    //
    // THIS USED TO BE TWO PRIVATE HELPERS spelled out here and again —
    // differently — in logos-basecamp. The divergence was not cosmetic: this
    // file called registerPluginIdentity BEFORE apiForPlugin in the backend
    // branch, i.e. it registered a credential at capability_module before the
    // identity's store existed. That was harmless only because the credential
    // was discarded either way. There is now one implementation and no order
    // for a host to get wrong.
    //
    // A falsy ConsumerIdentity is fatal for the plugin rather than a cue to
    // fall back to hostApi().
    logos::ConsumerIdentity consumerFor(const QString& name);

    LogosAPI* m_hostApi = nullptr;
    QHash<QString, logos::ConsumerIdentity> m_consumers;
};
