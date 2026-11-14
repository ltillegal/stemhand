#include "cli.h"

#include "engine/audioio.h"
#include "engine/gtcrn.h"
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

namespace photone {
namespace {

struct ResolvedModel {
    QString directory;
    ModelManifest manifest;
    QString weightsPath;
};

bool resolveModel(const DenoiseRequest &request, ResolvedModel *resolved, QString *error)
{
    QString directory = request.modelRef;

    if (directory.isEmpty() || !QFileInfo::exists(directory + QStringLiteral("/manifest.json"))) {
        ModelStore store;
        if (!request.storeRoot.isEmpty())
            store.setRoot(request.storeRoot);
        store.rescan();

        QString wanted = request.modelRef;
        bool found = false;
        for (const QVariant &item : store.models()) {
            const QVariantMap map = item.toMap();
            const bool driverMatches = map.value(QStringLiteral("driver")).toString()
                == QLatin1String("gtcrn");
            const bool idMatches = wanted.isEmpty()
                || map.value(QStringLiteral("id")).toString() == wanted;
            if (driverMatches && idMatches) {
                directory = map.value(QStringLiteral("dir")).toString();
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) {
                *error = wanted.isEmpty()
                    ? QStringLiteral("no model with driver \"gtcrn\" is installed")
                    : QStringLiteral("no installed model matches \"%1\" with driver \"gtcrn\"")
                          .arg(wanted);
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
    if (manifest->driver != QLatin1String("gtcrn")) {
        if (error) {
            *error = QStringLiteral("%1 %2 uses driver \"%3\", this command only runs \"gtcrn\"")
                         .arg(manifest->id, manifest->version, manifest->driver);
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
        std::fprintf(stderr, "photone --denoise needs an input and an output path\n");
        return 64;
    }
    if (!QFileInfo::exists(request.input)) {
        std::fprintf(stderr, "no such input: %s\n", qPrintable(request.input));
        return 66;
    }

    ResolvedModel model;
    QString error;
    if (!resolveModel(request, &model, &error)) {
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

int runUpdate(const UpdateRequest &request)
{
    if (!request.indexUrl.isValid()) {
        std::fprintf(stderr, "photone --update needs a valid index URL\n");
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

} // namespace photone
