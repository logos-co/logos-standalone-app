#pragma once

#include <QMainWindow>
#include <QString>

// logos::host::LogosCore — the core handle main() owns and this window borrows.
#include "logos_host_core.h"

class LogosAPI;
namespace logos::ui { class UiPluginLoader; }

// One UI plugin in a window. Loading is logos-view-module-runtime's
// UiPluginLoader, the pipeline Basecamp runs too: the plugin's dependencies,
// its own admitted identity, then its widget or sandboxed QML view.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `core` is BORROWED: main() constructs exactly one and outlives this window.
    explicit MainWindow(logos::host::LogosCore& core,
                        const QString& pluginPath,
                        const QString& title = QString(),
                        int width = 1024,
                        int height = 768,
                        QWidget* parent = nullptr);
    ~MainWindow() override = default;

private:
    void showMessage(const QString& text);

    logos::host::LogosCore& m_core;
    // This process speaking as itself: the trusted channel admission registers
    // each plugin's credential over. Plugins never get it.
    LogosAPI* m_hostApi = nullptr;
    logos::ui::UiPluginLoader* m_loader = nullptr;
};
