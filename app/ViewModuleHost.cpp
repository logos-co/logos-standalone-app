#include "ViewModuleHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QDebug>
#include <QTimer>

ViewModuleHost::ViewModuleHost(QObject* parent)
    : QObject(parent)
{
}

ViewModuleHost::~ViewModuleHost()
{
    stop();
}

bool ViewModuleHost::spawn(const QString& moduleName, const QString& pluginPath)
{
    if (m_process) {
        qWarning() << "ViewModuleHost: process already running for" << m_moduleName;
        return false;
    }

    m_moduleName = moduleName;

    // Generate a unique socket name
    QString uniqueId = QUuid::createUuid().toString(QUuid::Id128).left(8);
    m_socketName = QStringLiteral("logos_ui_%1_%2").arg(moduleName, uniqueId);

    // Find the ui-host binary (same directory as the app binary)
    QString appDir = QCoreApplication::applicationDirPath();
    QString uiHostPath = QDir(appDir).filePath("ui-host");

#ifdef Q_OS_WIN
    uiHostPath += ".exe";
#endif

    if (!QFile::exists(uiHostPath)) {
        qWarning() << "ViewModuleHost: ui-host binary not found at" << uiHostPath;
        return false;
    }

    m_process = new QProcess(this);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        qDebug() << "ViewModuleHost: process exited for" << m_moduleName << "with code" << exitCode;
        m_process->deleteLater();
        m_process = nullptr;
        emit processExited(exitCode);
    });

    // Watch stdout for the READY signal, buffering across chunk boundaries
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer += m_process->readAllStandardOutput();
        if (!m_ready && m_stdoutBuffer.contains("READY")) {
            m_ready = true;
            qDebug() << "ViewModuleHost: process ready for" << m_moduleName;
            emit ready();
        }
    });

    // Forward stderr for debugging
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        QByteArray data = m_process->readAllStandardError();
        qDebug() << "ui-host [" << m_moduleName << "]:" << data.trimmed();
    });

    QStringList args;
    args << "--name" << moduleName
         << "--path" << pluginPath
         << "--socket" << m_socketName;

    qDebug() << "ViewModuleHost: spawning" << uiHostPath << args;
    m_process->start(uiHostPath, args);

    if (!m_process->waitForStarted(5000)) {
        qWarning() << "ViewModuleHost: failed to start ui-host for" << moduleName;
        delete m_process;
        m_process = nullptr;
        return false;
    }

    return true;
}

void ViewModuleHost::stop()
{
    if (!m_process) {
        return;
    }

    qDebug() << "ViewModuleHost: stopping process for" << m_moduleName;

    m_process->terminate();

    if (!m_process->waitForFinished(3000)) {
        qWarning() << "ViewModuleHost: process did not exit gracefully, killing" << m_moduleName;
        m_process->kill();
        m_process->waitForFinished(1000);
    }

    delete m_process;
    m_process = nullptr;
    m_stdoutBuffer.clear();
    m_ready = false;
}

bool ViewModuleHost::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

QString ViewModuleHost::socketName() const
{
    return m_socketName;
}
