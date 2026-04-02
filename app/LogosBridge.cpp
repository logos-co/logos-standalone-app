#include "LogosBridge.h"

#include <QJSEngine>
#include <QRemoteObjectNode>
#include <QRemoteObjectDynamicReplica>
#include <QRemoteObjectPendingCall>
#include <QRemoteObjectPendingCallWatcher>
#include <QTimer>
#include <QDebug>

#include "logos_api.h"
#include "logos_api_client.h"

LogosBridge::LogosBridge(LogosAPI* api, QObject* parent)
    : QObject(parent), m_api(api)
{
}

QVariant LogosBridge::callModule(const QString& moduleName,
                                 const QString& method,
                                 const QVariantList& args)
{
    if (!m_api) return {};
    LogosAPIClient* client = m_api->getClient(moduleName);
    if (!client || !client->isConnected()) return {};
    return client->invokeRemoteMethod(moduleName, method, args);
}

void LogosBridge::setViewModuleSocket(const QString& moduleName, const QString& socketName)
{
    m_viewModuleSockets[moduleName] = socketName;
}

QRemoteObjectDynamicReplica* LogosBridge::getOrCreateReplica(const QString& moduleName)
{
    auto it = m_replicas.constFind(moduleName);
    if (it != m_replicas.cend()) return it.value();

    auto socketIt = m_viewModuleSockets.constFind(moduleName);
    if (socketIt == m_viewModuleSockets.cend()) return nullptr;

    auto* node = new QRemoteObjectNode(this);
    node->connectToNode(QUrl(QStringLiteral("local:") + socketIt.value()));
    auto* replica = node->acquireDynamic(moduleName);
    m_replicaNodes[moduleName] = node;
    m_replicas[moduleName] = replica;
    return replica;
}

void LogosBridge::invokeOnReplica(QRemoteObjectDynamicReplica* replica,
                                   const QString& method,
                                   const QVariantList& args,
                                   QJSValue callback)
{
    QRemoteObjectPendingCall pendingCall;
    bool ok = QMetaObject::invokeMethod(replica, "callMethod",
        Qt::DirectConnection,
        Q_RETURN_ARG(QRemoteObjectPendingCall, pendingCall),
        Q_ARG(QString, method),
        Q_ARG(QVariantList, args));

    if (!ok) {
        if (callback.isCallable())
            callback.call(QJSValueList() << QJSValue("{\"error\":\"Invocation failed\"}"));
        return;
    }

    auto* watcher = new QRemoteObjectPendingCallWatcher(pendingCall, this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [callback](QRemoteObjectPendingCallWatcher* w) mutable {
        if (callback.isCallable()) {
            if (w->error() == QRemoteObjectPendingCall::NoError)
                callback.call(QJSValueList() << QJSValue(w->returnValue().toString()));
            else
                callback.call(QJSValueList() << QJSValue("{\"error\":\"Remote call failed\"}"));
        }
        w->deleteLater();
    });
}

void LogosBridge::callModuleAsync(const QString& module,
                                   const QString& method,
                                   const QVariantList& args,
                                   QJSValue callback)
{
    if (!m_viewModuleSockets.contains(module)) {
        // Non-view module — use async LogosAPI IPC to avoid blocking the GUI thread
        if (!m_api) {
            if (callback.isCallable())
                callback.call(QJSValueList() << QJSValue("{\"error\":\"No API available\"}"));
            return;
        }
        LogosAPIClient* client = m_api->getClient(module);
        if (!client || !client->isConnected()) {
            if (callback.isCallable())
                callback.call(QJSValueList() << QJSValue("{\"error\":\"Module not connected\"}"));
            return;
        }
        client->invokeRemoteMethodAsync(module, method, args,
            [callback](QVariant result) mutable {
                if (callback.isCallable())
                    callback.call(QJSValueList() << QJSValue(result.toString()));
            });
        return;
    }

    // View module — call through the private QRO socket to ViewModuleProxy.
    auto* replica = getOrCreateReplica(module);
    if (!replica) {
        if (callback.isCallable())
            callback.call(QJSValueList() << QJSValue("{\"error\":\"Failed to connect to view module\"}"));
        return;
    }

    if (replica->isInitialized()) {
        invokeOnReplica(replica, method, args, callback);
        return;
    }

    // Wait for replica initialization via signal instead of blocking
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    connect(replica, &QRemoteObjectDynamicReplica::initialized, this,
            [this, replica, method, args, callback, timeout]() mutable {
        timeout->stop();
        timeout->deleteLater();
        invokeOnReplica(replica, method, args, callback);
    });
    connect(timeout, &QTimer::timeout, this,
            [callback, timeout]() mutable {
        timeout->deleteLater();
        if (callback.isCallable())
            callback.call(QJSValueList() << QJSValue("{\"error\":\"View module replica timeout\"}"));
    });
    timeout->start(10000);
}
