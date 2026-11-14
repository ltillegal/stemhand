#include "audiofile.h"
#include "modelstore.h"
#include "waveformitem.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDirIterator>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>

#include <sodium.h>

using namespace photone;

namespace {

int runScan(const QString &root, bool asJson)
{
    ModelStore store;
    if (!root.isEmpty())
        store.setRoot(root);
    store.rescan();

    const QVariantList models = store.models();

    if (asJson) {
        QJsonArray array;
        for (const QVariant &item : models) {
            const QVariantMap map = item.toMap();
            QJsonObject object;
            for (auto it = map.cbegin(); it != map.cend(); ++it) {
                if (it.key() == QLatin1String("dir") || it.key() == QLatin1String("stems"))
                    continue;
                object.insert(it.key(), QJsonValue::fromVariant(it.value()));
            }
            array.append(object);
        }

        QJsonObject rootObject;
        rootObject.insert(QStringLiteral("root"), store.root());
        rootObject.insert(QStringLiteral("summary"), store.summary());
        rootObject.insert(QStringLiteral("models"), array);

        const QByteArray output = QJsonDocument(rootObject).toJson(QJsonDocument::Indented);
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    } else {
        std::printf("%s\n", qPrintable(store.root()));
        std::printf("%s\n\n", qPrintable(store.summary()));
        for (const QVariant &item : models) {
            const QVariantMap map = item.toMap();
            std::printf("%-9s %-34s %-9s %-9s %s\n",
                        qPrintable(map.value(QStringLiteral("status")).toString()),
                        qPrintable(map.value(QStringLiteral("id")).toString() + QStringLiteral(" ")
                                   + map.value(QStringLiteral("version")).toString()),
                        qPrintable(map.value(QStringLiteral("kind")).toString()),
                        qPrintable(map.value(QStringLiteral("sizeLabel")).toString()),
                        qPrintable(map.value(QStringLiteral("detail")).toString()));
        }
    }

    for (const QVariant &item : models) {
        const QString status = item.toMap().value(QStringLiteral("status")).toString();
        if (status == QLatin1String("corrupt") || status == QLatin1String("broken")
            || status == QLatin1String("incompatible"))
            return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QStringList raw;
    raw.reserve(argc);
    for (int i = 0; i < argc; ++i)
        raw.append(QString::fromLocal8Bit(argv[i]));

    bool scan = false;
    bool asJson = false;
    QString root;
    QString screenshot;
    QString openPath;

    for (int i = 1; i < raw.size(); ++i) {
        const QString &arg = raw.at(i);
        if (arg == QLatin1String("--scan")) {
            scan = true;
        } else if (arg == QLatin1String("--json")) {
            asJson = true;
        } else if (arg == QLatin1String("--root") && i + 1 < raw.size()) {
            root = raw.at(++i);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < raw.size()) {
            screenshot = raw.at(++i);
        } else if (arg == QLatin1String("--version")) {
            std::printf("photone %s\n", PHOTONE_VERSION);
            return 0;
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            std::printf(
                "photone %s\n\n"
                "  photone [file]            open the app\n"
                "  photone --scan [--json]   list the model store, exit 1 if a model is broken\n"
                "  photone --root <dir>      use another model store\n",
                PHOTONE_VERSION);
            return 0;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            openPath = arg;
        }
    }

    // Scan mode has to work from a terminal and from agents with no display,
    // so it never constructs a GUI application (and never touches the session).
    if (scan) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("photone"));
        if (sodium_init() < 0) {
            std::fprintf(stderr, "photone: libsodium failed to initialise\n");
            return 1;
        }
        return runScan(root, asJson);
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("photone"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PHOTONE_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("photone"));

    if (sodium_init() < 0) {
        std::fprintf(stderr, "photone: libsodium failed to initialise\n");
        return 1;
    }

    ModelStore store;
    if (!root.isEmpty())
        store.setRoot(root);
    store.rescan();

    AudioFile audio;

    qmlRegisterType<WaveformItem>("Photone", 1, 0, "WaveformItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("store"), &store);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QStringLiteral(PHOTONE_VERSION));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return 1;

    if (!openPath.isEmpty())
        audio.load(QUrl::fromLocalFile(openPath));

    if (!screenshot.isEmpty()) {
        QTimer::singleShot(1200, &engine, [&engine, screenshot] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
            if (!window) {
                QCoreApplication::exit(2);
                return;
            }
            const bool saved = window->grabWindow().save(screenshot);
            QCoreApplication::exit(saved ? 0 : 3);
        });
    }

    return app.exec();
}
