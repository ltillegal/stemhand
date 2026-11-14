#include "denoisecontroller.h"

#include "engine/audioio.h"
#include "engine/gtcrn.h"
#include "modelstore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>

namespace stemhand {
namespace {

struct JobResult {
    bool ok = false;
    QString error;
    QString output;
    double audioSeconds = 0.0;
    double wallSeconds = 0.0;
};

JobResult runJob(const QString &input, const QString &output, const QString &modelPath)
{
    JobResult result;
    result.output = output;

    // Per-frame graphs do not scale: on the 2-core dev box one thread is
    // 4.9x realtime and four threads are 2.7x, because synchronisation costs
    // more than the convolutions save.
    const int threads = 1;

    GtcrnDenoiser denoiser;
    if (!denoiser.load(modelPath, threads, &result.error))
        return result;

    QVector<float> decoded;
    if (!decodeToMono(input, denoiser.sampleRate(), &decoded, &result.error))
        return result;

    std::vector<float> samples(decoded.begin(), decoded.end());
    result.audioSeconds = samples.size() / static_cast<double>(denoiser.sampleRate());

    QElapsedTimer timer;
    timer.start();
    if (!denoiser.process(samples, &result.error))
        return result;
    result.wallSeconds = timer.elapsed() / 1000.0;

    QVector<float> outputSamples(samples.begin(), samples.end());
    preventClipping(&outputSamples);
    if (!writeWav(output, outputSamples, denoiser.sampleRate(), &result.error))
        return result;

    result.ok = true;
    return result;
}

} // namespace

DenoiseController::DenoiseController(ModelStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &ModelStore::changed, this, &DenoiseController::voiceModelChanged);
}

bool DenoiseController::hasVoiceModel() const
{
    for (const QVariant &item : m_store->models()) {
        if (item.toMap().value(QStringLiteral("driver")).toString() == QLatin1String("gtcrn"))
            return true;
    }
    return false;
}

QString DenoiseController::suggestedOutput(const QUrl &input) const
{
    const QFileInfo info(input.toLocalFile());
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
        + QStringLiteral(" (stemhand denoised).wav");
}

void DenoiseController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void DenoiseController::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit messageChanged();
}

void DenoiseController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void DenoiseController::run(const QUrl &input)
{
    if (m_busy)
        return;

    const QString source = input.toLocalFile();
    if (source.isEmpty()) {
        setError(tr("Only local files can be processed."));
        return;
    }

    QString modelPath;
    QString modelLabel;
    for (const QVariant &item : m_store->models()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("driver")).toString() != QLatin1String("gtcrn"))
            continue;
        if (map.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
            continue;
        modelPath = map.value(QStringLiteral("weights")).toString();
        modelLabel = map.value(QStringLiteral("name")).toString();
        break;
    }

    if (modelPath.isEmpty()) {
        setError(tr("No verified GTCRN model is installed. Import one, or run scripts/install-gtcrn.sh."));
        return;
    }

    const QString output = suggestedOutput(input);
    setError(QString());
    setMessage(tr("Denoising with %1…").arg(modelLabel));
    setBusy(true);

    auto *watcher = new QFutureWatcher<JobResult>(this);
    connect(watcher, &QFutureWatcher<JobResult>::finished, this, [this, watcher] {
        const JobResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.ok) {
            setMessage(QString());
            setError(result.error);
            return;
        }

        m_lastOutput = result.output;
        setMessage(tr("Denoised %1 s of audio in %2 s (%3× realtime) → %4")
                       .arg(result.audioSeconds, 0, 'f', 1)
                       .arg(result.wallSeconds, 0, 'f', 2)
                       .arg(result.wallSeconds > 0 ? result.audioSeconds / result.wallSeconds : 0.0,
                            0, 'f', 1)
                       .arg(QFileInfo(result.output).fileName()));
        emit finished(result.output);
    });

    watcher->setFuture(QtConcurrent::run([source, output, modelPath] {
        return runJob(source, output, modelPath);
    }));
}

} // namespace stemhand
