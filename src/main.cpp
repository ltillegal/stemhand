#include "audiofile.h"
#include "cli.h"
#include "denoisecontroller.h"
#include "updatecontroller.h"
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

using namespace stemhand;

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
    QString denoiseIn;
    QString denoiseOut;
    QString separateIn;
    QString separateOut;
    QString modelRef;
    QString updateIndex;
    QString preferRef;
    QString releaseId;
    bool listVersions = false;
    bool checkOnly = false;
    int threads = 0;

    for (int i = 1; i < raw.size(); ++i) {
        const QString &arg = raw.at(i);
        if (arg == QLatin1String("--scan")) {
            scan = true;
        } else if (arg == QLatin1String("--json")) {
            asJson = true;
        } else if (arg == QLatin1String("--denoise") && i + 2 < raw.size()) {
            denoiseIn = raw.at(++i);
            denoiseOut = raw.at(++i);
        } else if (arg == QLatin1String("--update") && i + 1 < raw.size()) {
            updateIndex = raw.at(++i);
        } else if (arg == QLatin1String("--prefer") && i + 1 < raw.size()) {
            preferRef = raw.at(++i);
        } else if (arg == QLatin1String("--release") && i + 1 < raw.size()) {
            releaseId = raw.at(++i);
        } else if (arg == QLatin1String("--versions")) {
            listVersions = true;
        } else if (arg == QLatin1String("--check")) {
            checkOnly = true;
        } else if (arg == QLatin1String("--separate") && i + 2 < raw.size()) {
            separateIn = raw.at(++i);
            separateOut = raw.at(++i);
        } else if (arg == QLatin1String("--model") && i + 1 < raw.size()) {
            modelRef = raw.at(++i);
        } else if (arg == QLatin1String("--threads") && i + 1 < raw.size()) {
            threads = raw.at(++i).toInt();
        } else if (arg == QLatin1String("--root") && i + 1 < raw.size()) {
            root = raw.at(++i);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < raw.size()) {
            screenshot = raw.at(++i);
        } else if (arg == QLatin1String("--version")) {
            std::printf("stemhand %s\n", STEMHAND_VERSION);
            return 0;
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            std::printf(
                "stemhand %s\n\n"
                "  stemhand [file]            open the app\n"
                "  stemhand --scan [--json]   list the model store, exit 1 if a model is broken\n"
                "  stemhand --denoise <in> <out> [--model <id|dir>] [--threads N]\n"
                "  stemhand --update <index-url> [--check]\n"
                "  stemhand --prefer <id>@<version> | --release <id> | --versions\n"
                "  stemhand --root <dir>      use another model store\n",
                STEMHAND_VERSION);
            return 0;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            openPath = arg;
        }
    }

    // Scan mode has to work from a terminal and from agents with no display,
    // so it never constructs a GUI application (and never touches the session).
    const bool headless = scan || !denoiseIn.isEmpty() || !separateIn.isEmpty()
        || !updateIndex.isEmpty()
        || !preferRef.isEmpty() || !releaseId.isEmpty() || listVersions;

    if (headless) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("stemhand"));
        if (sodium_init() < 0) {
            std::fprintf(stderr, "stemhand: libsodium failed to initialise\n");
            return 1;
        }
        if (scan)
            return runScan(root, asJson);

        if (!preferRef.isEmpty())
            return runPrefer(preferRef, root);
        if (!releaseId.isEmpty())
            return runRelease(releaseId, root);
        if (listVersions)
            return runVersions(root);

        if (!separateIn.isEmpty()) {
            SeparateRequest request;
            request.input = separateIn;
            request.outputDir = separateOut;
            request.modelRef = modelRef;
            request.storeRoot = root;
            request.threads = threads;
            return runSeparate(request);
        }

        if (!updateIndex.isEmpty()) {
            UpdateRequest request;
            request.indexUrl = QUrl(updateIndex);
            request.storeRoot = root;
            request.checkOnly = checkOnly;
            return runUpdate(request);
        }

        DenoiseRequest request;
        request.input = denoiseIn;
        request.output = denoiseOut;
        request.modelRef = modelRef;
        request.storeRoot = root;
        request.threads = threads;
        return runDenoise(request);
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("stemhand"));
    QGuiApplication::setApplicationVersion(QStringLiteral(STEMHAND_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("stemhand"));

    if (sodium_init() < 0) {
        std::fprintf(stderr, "stemhand: libsodium failed to initialise\n");
        return 1;
    }

    ModelStore store;
    if (!root.isEmpty())
        store.setRoot(root);
    store.rescan();

    AudioFile audio;
    DenoiseController denoise(&store);
    UpdateController updates(&store);
    QObject::connect(&denoise, &DenoiseController::finished, &audio,
                     [&audio](const QString &path) { audio.load(QUrl::fromLocalFile(path)); });

    qmlRegisterType<WaveformItem>("Stemhand", 1, 0, "WaveformItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("store"), &store);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("denoise"), &denoise);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QStringLiteral(STEMHAND_VERSION));
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
