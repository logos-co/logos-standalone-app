// ui-host: Out-of-process host for view module C++ plugins.
// Loads a plugin, exposes it via QRemoteObjects on a private socket.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QPluginLoader>
#include <QRemoteObjectHost>
#include <QMetaMethod>
#include <QTextStream>
#include <QDebug>

#include "logos_api.h"

// Proxy exposed via QRO. Coerces arg types so callers don't need exact signatures.
class ViewModuleProxy : public QObject
{
    Q_OBJECT
public:
    explicit ViewModuleProxy(QObject* plugin, QObject* parent = nullptr)
        : QObject(parent), m_plugin(plugin) {}

    Q_INVOKABLE QVariant callMethod(const QString& methodName, const QVariantList& args)
    {
        if (!m_plugin) return {};

        const QMetaObject* mo = m_plugin->metaObject();
        const QByteArray name = methodName.toUtf8();

        // Find method by name + param count
        QMetaMethod method;
        for (int i = 0; i < mo->methodCount(); ++i) {
            QMetaMethod m = mo->method(i);
            if (m.name() == name && m.parameterCount() == args.size()) {
                method = m;
                break;
            }
        }
        if (!method.isValid()) {
            qWarning() << "ViewModuleProxy: method not found:" << methodName
                       << "with" << args.size() << "args";
            return {};
        }

        // Coerce args to expected types
        QVariantList coerced = args;
        for (int i = 0; i < method.parameterCount() && i < coerced.size(); ++i) {
            QMetaType target = method.parameterMetaType(i);
            if (coerced[i].metaType() != target) {
                QVariant c = coerced[i];
                if (!c.convert(target)) {
                    qWarning() << "ViewModuleProxy: failed to convert arg" << i
                               << "to" << target.name() << "for method" << methodName;
                    return {};
                }
                coerced[i] = c;
            }
        }

        // Build argument array
        QGenericArgument ga[10]{};
        for (int i = 0; i < coerced.size() && i < 10; ++i)
            ga[i] = QGenericArgument(coerced[i].typeName(), coerced[i].constData());

        // Invoke — void vs returning
        QMetaType retType = method.returnMetaType();
        if (!retType.isValid() || retType.id() == QMetaType::Void) {
            if (!method.invoke(m_plugin, Qt::DirectConnection,
                    ga[0], ga[1], ga[2], ga[3], ga[4],
                    ga[5], ga[6], ga[7], ga[8], ga[9])) {
                qWarning() << "ViewModuleProxy: invoke failed for void method" << methodName;
            }
            return {};
        }

        void* retData = retType.create();
        QGenericReturnArgument retArg(retType.name(), retData);

        bool ok = method.invoke(m_plugin, Qt::DirectConnection, retArg,
            ga[0], ga[1], ga[2], ga[3], ga[4],
            ga[5], ga[6], ga[7], ga[8], ga[9]);

        if (!ok) {
            qWarning() << "ViewModuleProxy: invoke failed for method" << methodName;
        }

        QVariant result = ok ? QVariant(retType, retData) : QVariant();
        retType.destroy(retData);
        return result;
    }

private:
    QObject* m_plugin;
};

#include "main.moc"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ui-host");

    QCommandLineParser parser;
    parser.setApplicationDescription("Logos UI module host process");
    parser.addHelpOption();

    QCommandLineOption nameOpt(QStringList() << "name",
                               "Module name", "module_name");
    QCommandLineOption pathOpt(QStringList() << "path",
                               "Path to the plugin .so/.dylib", "plugin_path");
    QCommandLineOption socketOpt(QStringList() << "socket",
                                 "Local socket name for QRemoteObjectHost", "socket_name");

    parser.addOption(nameOpt);
    parser.addOption(pathOpt);
    parser.addOption(socketOpt);
    parser.process(app);

    if (!parser.isSet(nameOpt) || !parser.isSet(pathOpt) || !parser.isSet(socketOpt)) {
        qCritical() << "Usage: ui-host --name <module_name> --path <plugin.so> --socket <socket_name>";
        return 1;
    }

    const QString moduleName = parser.value(nameOpt);
    const QString pluginPath = parser.value(pathOpt);
    const QString socketName = parser.value(socketOpt);

    QPluginLoader loader(pluginPath);
    if (!loader.load()) {
        qCritical() << "Failed to load plugin:" << loader.errorString();
        return 1;
    }

    QObject* pluginObject = loader.instance();
    if (!pluginObject) {
        qCritical() << "Failed to get plugin instance:" << loader.errorString();
        return 1;
    }

    qDebug() << "ui-host: loaded plugin" << moduleName << "from" << pluginPath;

    // Give the plugin a LogosAPI so it can call backend modules
    LogosAPI* logosAPI = new LogosAPI(moduleName, pluginObject);
    if (pluginObject->metaObject()->indexOfMethod("initLogos(LogosAPI*)") != -1) {
        QMetaObject::invokeMethod(pluginObject, "initLogos",
                                  Qt::DirectConnection,
                                  Q_ARG(LogosAPI*, logosAPI));
        qDebug() << "ui-host: called initLogos on plugin" << moduleName;
    }

    auto* proxy = new ViewModuleProxy(pluginObject);

    QRemoteObjectHost host(QUrl(QStringLiteral("local:") + socketName));
    if (!host.enableRemoting(proxy, moduleName)) {
        qCritical() << "Failed to enable remoting for" << moduleName;
        return 1;
    }

    qDebug() << "ui-host: remoting enabled on local:" << socketName;

    QTextStream out(stdout);
    out << "READY" << Qt::endl;
    out.flush();

    return app.exec();
}
