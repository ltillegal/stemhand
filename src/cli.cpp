#include "cli.h"

#include "engine/audioio.h"
#include "engine/gtcrn.h"
#include "engine/batchrunner.h"
#include "engine/htdemucs.h"
#include "engine/stemmixer.h"
#include "manifest.h"
#include "modelstore.h"
#include "registry.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QUrl>

#include <cstdio>

#include <sodium.h>

namespace stemhand {
namespace {

struct ResolvedModel {
    QString directory;
    ModelManifest manifest;
    QString weightsPath;
};

bool resolveModel(const QString &modelRef, const QString &storeRoot, const QString &driver,
                  ResolvedModel *resolved, QString *error)
{
    QString directory = modelRef;

    if (directory.isEmpty() || !QFileInfo::exists(directory + QStringLiteral("/manifest.json"))) {
        ModelStore store;
        if (!storeRoot.isEmpty())
            store.setRoot(storeRoot);
        store.rescan();

        bool found = false;
        for (const QVariant &item : store.models()) {
            const QVariantMap map = item.toMap();
            const bool driverMatches =
                map.value(QStringLiteral("driver")).toString() == driver;
            const bool idMatches =
                modelRef.isEmpty() || map.value(QStringLiteral("id")).toString() == modelRef;
            if (driverMatches && idMatches) {
                directory = map.value(QStringLiteral("dir")).toString();
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) {
                *error = modelRef.isEmpty()
                    ? QStringLiteral("no model with driver \"%1\" is installed").arg(driver)
                    : QStringLiteral("no installed model matches \"%1\" with driver \"%2\"")
                          .arg(modelRef, driver);
            }
            return false;
        }
    }

    QFile manifestFile(directory + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1/manifest.json").arg(directory);
        return false;
    }

    QString parseError;
    auto manifest = ModelManifest::parse(manifestFile.readAll(), &parseError);
    if (!manifest) {
        if (error)
            *error = parseError;
        return false;
    }
    if (manifest->driver != driver) {
        if (error) {
            *error = QStringLiteral("%1 %2 uses driver \"%3\", expected \"%4\"")
                         .arg(manifest->id, manifest->version, manifest->driver, driver);
        }
        return false;
    }
    if (manifest->files.isEmpty()) {
        if (error)
            *error = QStringLiteral("model lists no weights");
        return false;
    }

    resolved->directory = directory;
    resolved->manifest = *manifest;
    resolved->weightsPath = directory + QLatin1Char('/') + manifest->files.first().path;
    return true;
}

} // namespace

int runDenoise(const DenoiseRequest &request)
{
    if (request.input.isEmpty() || request.output.isEmpty()) {
        std::fprintf(stderr, "stemhand --denoise needs an input and an output path\n");
        return 64;
    }
    if (!QFileInfo::exists(request.input)) {
        std::fprintf(stderr, "no such input: %s\n", qPrintable(request.input));
        return 66;
    }

    ResolvedModel model;
    QString error;
    if (!resolveModel(request.modelRef, request.storeRoot, QStringLiteral("gtcrn"), &model,
                      &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    // One thread wins for per-frame graphs: four threads on the 2-core dev box
    // measured 2.7x realtime against 4.9x for one. Override with --threads.
    const int threads = request.threads > 0 ? request.threads : 1;

    GtcrnDenoiser denoiser;
    if (!denoiser.load(model.weightsPath, threads, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    std::vector<float> samples;
    QVector<float> decoded;
    if (!decodeToMono(request.input, denoiser.sampleRate(), &decoded, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    samples.assign(decoded.begin(), decoded.end());

    const double duration = samples.size() / static_cast<double>(denoiser.sampleRate());

    QElapsedTimer timer;
    timer.start();
    if (!denoiser.process(samples, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    const double elapsed = timer.elapsed() / 1000.0;

    QVector<float> output(samples.begin(), samples.end());
    const float gain = preventClipping(&output);
    if (!writeWav(request.output, output, denoiser.sampleRate(), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    std::printf("model    %s %s (driver %s)\n", qPrintable(model.manifest.id),
                qPrintable(model.manifest.version), qPrintable(model.manifest.driver));
    std::printf("input    %s  %.2fs  %d Hz mono\n", qPrintable(request.input), duration,
                denoiser.sampleRate());
    std::printf("output   %s\n", qPrintable(request.output));
    std::printf("threads  %d\n", threads);
    std::printf("elapsed  %.3fs  %.1fx realtime\n", elapsed, duration / qMax(elapsed, 1e-6));
    if (gain < 1.0f)
        std::printf("gain     %.4f (peak limiting)\n", gain);
    return 0;
}

int runSeparate(const SeparateRequest &request)
{
    if (request.input.isEmpty() || request.outputDir.isEmpty()) {
        std::fprintf(stderr, "stemhand --separate needs an input file and an output directory\n");
        return 64;
    }
    if (!QFileInfo::exists(request.input)) {
        std::fprintf(stderr, "no such input: %s\n", qPrintable(request.input));
        return 66;
    }

    ResolvedModel model;
    QString error;
    if (!resolveModel(request.modelRef, request.storeRoot, QStringLiteral("htdemucs"), &model,
                      &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    const int threads =
        request.threads > 0 ? request.threads : std::max(1, QThread::idealThreadCount() / 2);

    HtdemucsSeparator separator;
    if (!separator.load(model.weightsPath, threads, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    QVector<float> interleaved;
    if (!decodeToChannels(request.input, HtdemucsSeparator::kSampleRate,
                          HtdemucsSeparator::kChannels, &interleaved, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    const double duration =
        interleaved.size() / (HtdemucsSeparator::kChannels
                              * static_cast<double>(HtdemucsSeparator::kSampleRate));

    const QString base = QFileInfo(request.input).completeBaseName();
    QElapsedTimer timer;
    timer.start();

    const bool ok = separator.separate(
        interleaved, request.outputDir, base, &error, [](int done, int total) {
            if (total > 1)
                std::fprintf(stderr, "  chunk %d/%d\n", done, total);
        });
    const double elapsed = timer.elapsed() / 1000.0;

    if (!ok) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    std::printf("model    %s %s (driver %s)\n", qPrintable(model.manifest.id),
                qPrintable(model.manifest.version), qPrintable(model.manifest.driver));
    std::printf("input    %s  %.2fs  %d Hz stereo\n", qPrintable(request.input), duration,
                HtdemucsSeparator::kSampleRate);
    std::printf("threads  %d\n", threads);
    std::printf("elapsed  %.1fs  %.2fx realtime\n", elapsed, duration / qMax(elapsed, 1e-6));
    for (const QString &stem : HtdemucsSeparator::stemNames()) {
        std::printf("stem     %s/%s_%s.wav\n", qPrintable(request.outputDir), qPrintable(base),
                    qPrintable(stem));
    }
    return 0;
}

int runBatch(const BatchRequest &request)
{
    if (request.inputs.isEmpty() || request.outputDir.isEmpty()) {
        std::fprintf(stderr, "stemhand --batch needs an output directory and at least one file\n");
        return 64;
    }

    QStringList inputs;
    for (const QString &input : request.inputs) {
        if (!QFileInfo::exists(input)) {
            std::fprintf(stderr, "skipping missing input: %s\n", qPrintable(input));
            continue;
        }
        inputs.append(input);
    }
    if (inputs.isEmpty()) {
        std::fprintf(stderr, "nothing to do: none of the inputs exist\n");
        return 66;
    }

    const QString driver = request.operation == BatchOperation::Denoise
        ? QStringLiteral("gtcrn")
        : QStringLiteral("htdemucs");

    ResolvedModel model;
    QString error;
    if (!resolveModel(request.modelRef, request.storeRoot, driver, &model, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    // Denoising does not benefit from threads; separation roughly halves its
    // time per doubling until the physical cores run out.
    const int threads = request.threads > 0
        ? request.threads
        : (request.operation == BatchOperation::Denoise
               ? 1
               : std::max(1, QThread::idealThreadCount() / 2));

    BatchRunner runner(request.operation);
    if (!runner.load(model.weightsPath, threads, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    std::printf("operation %s\n", qPrintable(BatchRunner::operationName(request.operation)));
    std::printf("model     %s %s\n", qPrintable(model.manifest.id),
                qPrintable(model.manifest.version));
    std::printf("output    %s\n", qPrintable(request.outputDir));
    std::printf("files     %d\n\n", static_cast<int>(inputs.size()));

    QVector<BatchResult> results;
    const QStringList batchInputs = inputs;
    const QString outputDirectory = request.outputDir;

    const bool completed = runner.run(
        batchInputs, outputDirectory, &results,
        [&batchInputs](int index, int total) {
            std::printf("[%d/%d] %s\n", index + 1, total,
                        qPrintable(QFileInfo(batchInputs.at(index)).fileName()));
            std::fflush(stdout);
        },
        {}, nullptr);

    int failed = 0;
    double wall = 0.0;
    double audio = 0.0;
    for (const BatchResult &result : results) {
        if (result.ok) {
            wall += result.wallSeconds;
            audio += result.audioSeconds;
            std::printf("  ok    %s  %.1fs → %s (%.1fx)\n",
                        qPrintable(QFileInfo(result.input).fileName()), result.wallSeconds,
                        qPrintable(QFileInfo(result.output).fileName()),
                        result.wallSeconds > 0 ? result.audioSeconds / result.wallSeconds : 0.0);
        } else {
            ++failed;
            std::printf("  FAIL  %s  %s\n", qPrintable(QFileInfo(result.input).fileName()),
                        qPrintable(result.error));
        }
    }

    std::printf("\n%d ok, %d failed", static_cast<int>(results.size()) - failed, failed);
    if (wall > 0.0)
        std::printf(", %.1fs of work for %.1fs of audio (%.1fx)", wall, audio, audio / wall);
    std::printf("\n");
    return (completed && failed == 0) ? 0 : 1;
}

int runMix(const MixRequest &request)
{
    if (request.stemsDir.isEmpty() || request.output.isEmpty()) {
        std::fprintf(stderr, "stemhand --mix needs a stems directory and an output file\n");
        return 64;
    }

    QDir dir(request.stemsDir);
    if (!dir.exists()) {
        std::fprintf(stderr, "no such directory: %s\n", qPrintable(request.stemsDir));
        return 66;
    }

    // Keep a stable order (drums, bass, other, vocals) so mixing is
    // reproducible regardless of directory enumeration.
    const QStringList order = HtdemucsSeparator::stemNames();
    QStringList paths;
    QVector<StemState> states;
    const QStringList files = dir.entryList({QStringLiteral("*.wav")}, QDir::Files, QDir::Name);
    for (const QString &name : order) {
        for (const QString &file : files) {
            if (!file.endsWith(QLatin1Char('_') + name + QStringLiteral(".wav")))
                continue;
            StemState state;
            if (request.muted.contains(name))
                state.muted = true;
            if (request.soloed.contains(name))
                state.solo = true;
            for (const auto &gain : request.gains) {
                if (gain.first == name)
                    state.gainDb = static_cast<float>(gain.second);
            }
            paths.append(dir.filePath(file));
            states.append(state);
        }
    }

    if (paths.isEmpty()) {
        std::fprintf(stderr, "no stems in %s (expected *_drums.wav and friends)\n",
                     qPrintable(request.stemsDir));
        return 66;
    }

    QString error;
    double gain = 1.0;
    double seconds = 0.0;
    if (!StemMixer::render(paths, states, request.output, &error, &gain, &seconds)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    std::printf("stems    %d\n", static_cast<int>(paths.size()));
    std::printf("output   %s  %.2fs\n", qPrintable(request.output), seconds);
    std::printf("gain     %.4f%s\n", gain, gain < 1.0 ? " (peak limiting)" : "");
    return 0;
}

int runUpdate(const UpdateRequest &request)
{
    if (!request.indexUrl.isValid()) {
        std::fprintf(stderr, "stemhand --update needs a valid index URL\n");
        return 64;
    }

    ModelStore store;
    if (!request.storeRoot.isEmpty())
        store.setRoot(request.storeRoot);
    store.rescan();

    RegistryIndex index;
    QString error;
    if (!parseIndex(request.indexUrl, &index, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    QList<UpdatePlan> plan;
    if (!planUpdates(index, &store, &plan, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    // Remember the registry so the app's "Update models" button works later.
    writeRegistryUrl(store.root(), request.indexUrl);

    if (plan.isEmpty()) {
        std::printf("up to date (%lld models in the index)\n",
                    static_cast<long long>(index.models.size()));
        return 0;
    }

    for (const UpdatePlan &item : plan) {
        std::printf("%-8s %s %s%s\n", item.isUpgrade ? "update" : "install",
                    qPrintable(item.id), qPrintable(item.version),
                    item.isUpgrade
                        ? qPrintable(QStringLiteral(" (from %1)").arg(item.installedVersion))
                        : "");
    }

    if (request.checkOnly)
        return 0;

    bool ok = true;
    for (const UpdatePlan &item : plan) {
        QString installed;
        if (!installModel(item.manifestUrl, store.root(), &installed, &error)) {
            std::fprintf(stderr, "failed to install %s %s: %s\n", qPrintable(item.id),
                         qPrintable(item.version), qPrintable(error));
            ok = false;
            continue;
        }
        std::printf("done     %s\n", qPrintable(installed));
    }

    store.rescan();
    return ok ? 0 : 1;
}

int runPrefer(const QString &reference, const QString &storeRoot)
{
    const int at = reference.lastIndexOf(QLatin1Char('@'));
    if (at <= 0 || at == reference.size() - 1) {
        std::fprintf(stderr, "expected <id>@<version>, e.g. denoise/gtcrn@1.0.0\n");
        return 64;
    }

    ModelStore store;
    if (!storeRoot.isEmpty())
        store.setRoot(storeRoot);
    store.rescan();

    const QString id = reference.left(at);
    const QString version = reference.mid(at + 1);

    if (!store.prefer(id, version)) {
        std::fprintf(stderr, "%s\n", qPrintable(store.lastError()));
        return 1;
    }
    std::printf("pinned   %s %s\n", qPrintable(id), qPrintable(version));
    return 0;
}

int runRelease(const QString &id, const QString &storeRoot)
{
    ModelStore store;
    if (!storeRoot.isEmpty())
        store.setRoot(storeRoot);
    store.rescan();
    store.releasePreference(id);
    std::printf("unpinned %s\n", qPrintable(id));
    return 0;
}

int runVersions(const QString &storeRoot)
{
    ModelStore store;
    if (!storeRoot.isEmpty())
        store.setRoot(storeRoot);
    store.rescan();

    QString current;
    for (const QVariant &item : store.models()) {
        const QVariantMap map = item.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        if (id != current) {
            if (!current.isEmpty())
                std::printf("\n");
            current = id;
            std::printf("%s\n", qPrintable(id));
        }
        std::printf("  %s %-10s %-12s %-9s %s\n",
                    map.value(QStringLiteral("preferred")).toBool() ? "*" : " ",
                    qPrintable(map.value(QStringLiteral("version")).toString()),
                    qPrintable(map.value(QStringLiteral("status")).toString()),
                    qPrintable(map.value(QStringLiteral("sizeLabel")).toString()),
                    map.value(QStringLiteral("preferred")).toBool() ? "pinned" : "");
    }
    return 0;
}

} // namespace stemhand
