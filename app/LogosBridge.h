#pragma once

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QJSValue>
#include <QMap>

class LogosAPI;
class QRemoteObjectNode;
class QRemoteObjectDynamicReplica;

class LogosBridge : public QObject
{
    Q_OBJECT
public:
    explicit LogosBridge(LogosAPI* api, QObject* parent = nullptr);

    Q_INVOKABLE QVariant callModule(const QString& moduleName,
                                    const QString& method,
                                    const QVariantList& args = {});

    Q_INVOKABLE void callModuleAsync(const QString& module,
                                     const QString& method,
                                     const QVariantList& args,
                                     QJSValue callback);

    void setViewModuleSocket(const QString& moduleName, const QString& socketName);

private:
    QRemoteObjectDynamicReplica* getOrCreateReplica(const QString& moduleName);
    void invokeOnReplica(QRemoteObjectDynamicReplica* replica,
                         const QString& method,
                         const QVariantList& args,
                         QJSValue callback);

    LogosAPI* m_api;
    QMap<QString, QString> m_viewModuleSockets;
    QMap<QString, QRemoteObjectNode*> m_replicaNodes;
    QMap<QString, QRemoteObjectDynamicReplica*> m_replicas;
};
