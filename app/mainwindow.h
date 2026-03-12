#pragma once

#include <QMainWindow>

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
};
