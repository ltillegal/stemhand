#include "audiofile.h"
#include "batchcontroller.h"
#include "exportcontroller.h"
#include "cli.h"
#include "denoisecontroller.h"
#include "engine/stemplayer.h"
#include "separatecontroller.h"
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
    QStringList openPaths;
    QString denoiseIn;
    QString denoiseOut;
    QString separateIn;
    QString separateOut;
    QString mixDir;
    QString mixOut;
    QString batchOperation;
    QString batchOutput;
    QStringList batchFiles;
    bool collectingBatch = false;
    QStringList muted;
    QStringList soloed;
    QList<QPair<QString, double>> gains;
    QString modelRef;
    QString updateIndex;
    QString preferRef;
    QString releaseId;
    bool listVersions = false;
    bool checkOnly = false;
    int threads = 0;

    for (int i = 1; i < raw.size(); ++i) {
        const QString &arg = raw.at(i);

        // `--batch` takes an operation, an output directory, then a plain list
        // of files; anything starting with a dash ends that list.
        if (arg.startsWith(QLatin1Char('-')) && arg != QLatin1String("--batch"))
            collectingBatch = false;

        if (arg == QLatin1String("--batch")) {
            if (i + 2 >= raw.size()) {
                std::fprintf(stderr,
                             "usage: stemhand --batch <denoise|separate> <outdir> <file> "
                             "[file...]\n");
                return 64;
            }
            batchOperation = raw.at(++i);
            batchOutput = raw.at(++i);
            collectingBatch = true;
        } else if (arg == QLatin1String("--scan")) {
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
        } else if (arg == QLatin1String("--mix") && i + 2 < raw.size()) {
            mixDir = raw.at(++i);
            mixOut = raw.at(++i);
        } else if (arg == QLatin1String("--mute") && i + 1 < raw.size()) {
            muted.append(raw.at(++i));
        } else if (arg == QLatin1String("--solo") && i + 1 < raw.size()) {
            soloed.append(raw.at(++i));
        } else if (arg == QLatin1String("--gain") && i + 1 < raw.size()) {
            const QString spec = raw.at(++i);
            const int equal = spec.indexOf(QLatin1Char('='));
            if (equal > 0)
                gains.append({spec.left(equal), spec.mid(equal + 1).toDouble()});
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
                "  stemhand [file]                    open the app\n"
                "  stemhand --scan [--json]           list the model store; exit 1 if broken\n"
                "  stemhand --denoise <in> <out>      clean up speech (GTCRN)\n"
                "  stemhand --separate <in> <outdir>  four stems (htdemucs)\n"
                "  stemhand --batch <denoise|separate> <outdir> <file> [file...]\n"
                "  stemhand --mix <stems-dir> <out.wav> [--mute <stem>] [--solo <stem>]\n"
                "                                     [--gain <stem>=<db>]\n"
                "  stemhand --update <index-url> [--check]\n"
                "  stemhand --prefer <id>@<version> | --release <id> | --versions\n"
                "\n"
                "  --model <id|dir>   pick a model explicitly\n"
                "  --threads N        override the thread count\n"
                "  --root <dir>       use another model store\n",
                STEMHAND_VERSION);
            return 0;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            if (collectingBatch)
                batchFiles.append(arg);
            else
                openPaths.append(arg);
        }
    }

    // Scan mode has to work from a terminal and from agents with no display,
    // so it never constructs a GUI application (and never touches the session).
    const bool headless = scan || !denoiseIn.isEmpty() || !separateIn.isEmpty()
        || !mixDir.isEmpty() || !batchFiles.isEmpty()
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

        if (!batchFiles.isEmpty()) {
            BatchRequest request;
            if (!BatchRunner::operationFromName(batchOperation, &request.operation)) {
                std::fprintf(stderr, "unknown batch operation \"%s\": use denoise or separate\n",
                             qPrintable(batchOperation));
                return 64;
            }
            request.inputs = batchFiles;
            request.outputDir = batchOutput;
            request.modelRef = modelRef;
            request.storeRoot = root;
            request.threads = threads;
            return runBatch(request);
        }

        if (!mixDir.isEmpty()) {
            MixRequest request;
            request.stemsDir = mixDir;
            request.output = mixOut;
            request.muted = muted;
            request.soloed = soloed;
            request.gains = gains;
            return runMix(request);
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

#if defined(Q_OS_LINUX)
    // Qt aborts inside platform-plugin init, leaving a core dump and a message
    // about xcb-cursor. Neither says the real cause: this shell has no display.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        std::fprintf(stderr,
                     "stemhand: no display is available to this shell.\n"
                     "  WAYLAND_DISPLAY and DISPLAY are both empty, which is what a\n"
                     "  terminal outside the graphical session looks like. Run it from a\n"
                     "  terminal inside your desktop session, or set them explicitly:\n"
                     "    WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/$(id -u) \\\n"
                     "      ./build/stemhand file.wav\n"
                     "  Headless commands (--scan, --denoise, --separate, --mix, --update)\n"
                     "  work without a display.\n");
        return 1;
    }
#endif

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
    SeparateController separate(&store);
    BatchController batch(&store);
    ExportController exporter;
    QObject::connect(&audio, &AudioFile::fileChanged, &exporter,
                     [&audio, &exporter] { exporter.keepSource(audio.url()); });
    StemPlayer player;
    QStringList loadedStemPaths;
    QObject::connect(&separate, &SeparateController::stemsChanged, &player, [&] {
        const QVariantList stems = separate.stems();
        QStringList paths;
        QVector<StemState> states;
        for (const QVariant &item : stems) {
            const QVariantMap map = item.toMap();
            paths.append(map.value(QStringLiteral("path")).toString());
            StemState state;
            state.gainDb = static_cast<float>(map.value(QStringLiteral("gain")).toDouble());
            state.muted = map.value(QStringLiteral("muted")).toBool();
            state.solo = map.value(QStringLiteral("solo")).toBool();
            states.append(state);
        }
        if (paths != loadedStemPaths) {
            loadedStemPaths = paths;
            player.load(paths, states);
        } else {
            player.setStates(states);
        }
    });
    QObject::connect(&audio, &AudioFile::fileChanged, &separate,
                     [&audio, &separate] { separate.discover(QUrl::fromLocalFile(audio.path())); });
    QObject::connect(&denoise, &DenoiseController::finished, &audio,
                     [&audio](const QString &path) { audio.load(QUrl::fromLocalFile(path)); });

    qmlRegisterType<WaveformItem>("Stemhand", 1, 0, "WaveformItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("store"), &store);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("denoise"), &denoise);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("separate"), &separate);
    engine.rootContext()->setContextProperty(QStringLiteral("batch"), &batch);
    engine.rootContext()->setContextProperty(QStringLiteral("exporter"), &exporter);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), &player);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QStringLiteral(STEMHAND_VERSION));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return 1;

    if (!openPaths.isEmpty())
        audio.load(QUrl::fromLocalFile(openPaths.first()));

    // Several files on the command line mean a batch: open the first, queue the
    // rest, so `stemhand *.wav` does something sensible.
    if (openPaths.size() > 1) {
        QList<QUrl> queued;
        for (const QString &path : openPaths)
            queued.append(QUrl::fromLocalFile(path));
        batch.addFiles(queued);
    }

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
