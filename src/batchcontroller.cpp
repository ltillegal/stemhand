#include "batchcontroller.h"

#include "engine/batchrunner.h"
#include "modelstore.h"

#include <QDesktopServices>
#include <QDir>

#include <algorithm>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

namespace stemhand {
namespace {

struct JobOutcome {
    bool completed = false;
    QString error;
    QVector<BatchResult> results;
    QString outputDirectory;
};

JobOutcome runBatch(BatchOperation operation, const QStringList &inputs,
                    const QString &outputDirectory, const QString &modelPath, int threads,
                    const std::function<void(int, int)> &fileStarted,
                    const std::atomic_bool *cancel)
{
    JobOutcome outcome;
    outcome.outputDirectory = outputDirectory;

    BatchRunner runner(operation);
    if (!runner.load(modelPath, threads, &outcome.error))
        return outcome;

    outcome.completed = runner.run(inputs, outputDirectory, &outcome.results, fileStarted, {},
                                   cancel);
    return outcome;
}

} // namespace

BatchController::BatchController(ModelStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &ModelStore::changed, this, &BatchController::hasModelChanged);
}

bool BatchController::hasModel() const
{
    const QString driver = m_operation == QLatin1String("denoise") ? QStringLiteral("gtcrn")
                                                                  : QStringLiteral("htdemucs");
    for (const QVariant &item : m_store->models()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("driver")).toString() == driver
            && map.value(QStringLiteral("status")).toString() == QLatin1String("ok"))
            return true;
    }
    return false;
}

void BatchController::setOperation(const QString &operation)
{
    if (m_operation == operation || m_busy)
        return;
    m_operation = operation;
    emit operationChanged();
    emit hasModelChanged();
}

void BatchController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BatchController::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit messageChanged();
}

void BatchController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void BatchController::openOutputFolder() const
{
    if (m_outputDirectory.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDirectory));
}

void BatchController::rebuildJobs()
{
    m_jobsVariant.clear();
    for (const Job &job : m_jobs) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), QFileInfo(job.path).fileName());
        map.insert(QStringLiteral("path"), job.path);
        map.insert(QStringLiteral("status"), job.status);
        map.insert(QStringLiteral("detail"), job.detail);
        m_jobsVariant.append(map);
    }
    emit jobsChanged();
}

void BatchController::addFiles(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        const bool known = std::any_of(m_jobs.cbegin(), m_jobs.cend(),
                                       [&path](const Job &job) { return job.path == path; });
        if (known)
            continue;
        Job job;
        job.path = path;
        job.status = QStringLiteral("pending");
        m_jobs.append(job);
    }
    rebuildJobs();
}

void BatchController::clear()
{
    if (m_busy)
        return;
    m_jobs.clear();
    m_message.clear();
    setError(QString());
    rebuildJobs();
    setMessage(QString());
}

void BatchController::cancel()
{
    if (!m_busy)
        return;
    m_cancel.store(true);
    setMessage(tr("Cancelling after the current file…"));
}

void BatchController::start()
{
    if (m_busy || m_jobs.isEmpty())
        return;

    QStringList inputs;
    QVector<int> queuedIndices; // row in m_jobs for each queued input
    for (int index = 0; index < m_jobs.size(); ++index) {
        if (m_jobs.at(index).status == QLatin1String("pending")) {
            inputs.append(m_jobs.at(index).path);
            queuedIndices.append(index);
        }
    }
    if (inputs.isEmpty()) {
        setError(tr("Nothing left to process. Add files or clear the queue."));
        return;
    }

    const QString driver = m_operation == QLatin1String("denoise") ? QStringLiteral("gtcrn")
                                                                  : QStringLiteral("htdemucs");
    QString modelPath;
    for (const QVariant &item : m_store->models()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("driver")).toString() != driver)
            continue;
        if (map.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
            continue;
        modelPath = map.value(QStringLiteral("weights")).toString();
        break;
    }
    if (modelPath.isEmpty()) {
        setError(tr("No verified model for this operation is installed."));
        return;
    }

    const bool denoise = m_operation == QLatin1String("denoise");
    // Denoise results go next to the queue's first file, in a folder named for
    // the operation; separation gets one folder per source file.
    const QString outputDirectory =
        QFileInfo(inputs.first()).absolutePath() + (denoise ? QStringLiteral("/stemhand denoised")
                                                            : QStringLiteral("/stemhand stems"));
    QDir().mkpath(outputDirectory);
    m_outputDirectory = outputDirectory;

    for (Job &job : m_jobs) {
        if (job.status != QLatin1String("failed"))
            job.status = QStringLiteral("pending");
        job.detail.clear();
    }
    rebuildJobs();

    m_cancel.store(false);
    setError(QString());
    setMessage(tr("Processing 0 of %1…").arg(inputs.size()));
    setBusy(true);

    const BatchOperation operation = denoise ? BatchOperation::Denoise : BatchOperation::Separate;
    const int threads = denoise ? 1 : std::max(1, QThread::idealThreadCount() / 2);
    const QStringList queued = inputs;

    auto *watcher = new QFutureWatcher<JobOutcome>(this);
    connect(watcher, &QFutureWatcher<JobOutcome>::finished, this, [this, watcher] {
        const JobOutcome outcome = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        int done = 0;
        int failed = 0;
        int cursor = 0;
        for (Job &job : m_jobs) {
            if (job.status == QLatin1String("ok") || job.status == QLatin1String("failed"))
                continue;
            if (cursor >= outcome.results.size()) {
                job.status = QStringLiteral("failed");
                job.detail = tr("not processed");
                ++failed;
                continue;
            }
            const BatchResult &result = outcome.results.at(cursor++);
            if (result.ok) {
                job.status = QStringLiteral("ok");
                job.detail = QStringLiteral("%1s → %2")
                                 .arg(result.wallSeconds, 0, 'f', 1)
                                 .arg(QFileInfo(result.output).fileName());
                ++done;
            } else {
                job.status = QStringLiteral("failed");
                job.detail = result.error;
                ++failed;
            }
        }
        rebuildJobs();

        if (!outcome.error.isEmpty()) {
            setMessage(QString());
            setError(outcome.error);
            return;
        }
        setMessage(tr("%1 done, %2 failed → %3")
                       .arg(done)
                       .arg(failed)
                       .arg(QFileInfo(outcome.outputDirectory).fileName()));
    });

    watcher->setFuture(QtConcurrent::run([this, operation, queued, queuedIndices, outputDirectory,
                                          modelPath, threads] {
        return runBatch(operation, queued, outputDirectory, modelPath, threads,
                        [this, queuedIndices](int index, int total) {
                            const int row = index < queuedIndices.size() ? queuedIndices.at(index)
                                                                         : -1;
                            QMetaObject::invokeMethod(this, [this, row, index, total] {
                                if (row >= 0 && row < m_jobs.size()) {
                                    m_jobs[row].status = QStringLiteral("running");
                                    rebuildJobs();
                                }
                                setMessage(tr("Processing %1 of %2…")
                                               .arg(index + 1)
                                               .arg(total));
                            }, Qt::QueuedConnection);
                        },
                        &m_cancel);
    }));
}

} // namespace stemhand
