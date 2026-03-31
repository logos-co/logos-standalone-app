#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class ViewModuleHost : public QObject {
    Q_OBJECT
public:
    explicit ViewModuleHost(QObject* parent = nullptr);
    ~ViewModuleHost();

    bool spawn(const QString& moduleName, const QString& pluginPath);
    void stop();
    bool isRunning() const;
    QString socketName() const;

signals:
    void processExited(int exitCode);
    void ready();

private:
    QProcess* m_process = nullptr;
    QString m_moduleName;
    QString m_socketName;
    QByteArray m_stdoutBuffer;
    bool m_ready = false;
};
