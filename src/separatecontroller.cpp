#include "separatecontroller.h"

#include "engine/audioio.h"
#include "engine/htdemucs.h"
#include "engine/stemmixer.h"
#include "modelstore.h"

#include <QDesktopServices>

#include <functional>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QThread>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

namespace stemhand {
namespace {

struct JobResult {
    bool ok = false;
    QString error;
    QString folder;
    QStringList stems;
    double audioSeconds = 0.0;
    double wallSeconds = 0.0;
};

JobResult runJob(const QString &input, const QString &folder, const QString &modelPath,
                 const std::function<void(int, int)> &progress)
{
    JobResult result;
    result.folder = folder;

    HtdemucsSeparator separator;
    if (!separator.load(modelPath, QThread::idealThreadCount(), &result.error))
        return result;

    QVector<float> interleaved;
    if (!decodeToChannels(input, HtdemucsSeparator::kSampleRate, HtdemucsSeparator::kChannels,
                          &interleaved, &result.error))
        return result;

    result.audioSeconds =
        interleaved.size() / (HtdemucsSeparator::kChannels
                              * static_cast<double>(HtdemucsSeparator::kSampleRate));

    const QString base = QFileInfo(input).completeBaseName();
    QElapsedTimer timer;
    timer.start();
    if (!separator.separate(interleaved, folder, base, &result.error, progress))
        return result;
    result.wallSeconds = timer.elapsed() / 1000.0;

    for (const QString &name : HtdemucsSeparator::stemNames())
        result.stems.append(folder + QLatin1Char('/') + base + QLatin1Char('_') + name
                            + QStringLiteral(".wav"));

    result.ok = true;
    return result;
}

} // namespace

SeparateController::SeparateController(ModelStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &ModelStore::changed, this, &SeparateController::hasStemModelChanged);
}

bool SeparateController::hasStemModel() const
{
    for (const QVariant &item : m_store->models()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("driver")).toString() == QLatin1String("htdemucs")
            && map.value(QStringLiteral("status")).toString() == QLatin1String("ok"))
            return true;
    }
    return false;
}

void SeparateController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SeparateController::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit messageChanged();
}

void SeparateController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void SeparateController::openStemsFolder() const
{
    if (m_folder.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
}

void SeparateController::rebuildStems()
{
    m_stems.clear();
    for (int i = 0; i < m_paths.size(); ++i) {
        const StemState &state = m_states.at(i);
        QVariantMap stem;
        stem.insert(QStringLiteral("name"),
                    QFileInfo(m_paths.at(i)).completeBaseName().section(QLatin1Char('_'), -1));
        stem.insert(QStringLiteral("path"), m_paths.at(i));
        stem.insert(QStringLiteral("url"), QUrl::fromLocalFile(m_paths.at(i)));
        stem.insert(QStringLiteral("gain"), state.gainDb);
        stem.insert(QStringLiteral("muted"), state.muted);
        stem.insert(QStringLiteral("solo"), state.solo);
        m_stems.append(stem);
    }
    emit stemsChanged();
}

void SeparateController::setGain(int index, double decibels)
{
    if (index < 0 || index >= m_states.size())
        return;
    m_states[index].gainDb = static_cast<float>(decibels);
    rebuildStems();
}

void SeparateController::toggleMute(int index)
{
    if (index < 0 || index >= m_states.size())
        return;
    m_states[index].muted = !m_states[index].muted;
    rebuildStems();
}

void SeparateController::toggleSolo(int index)
{
    if (index < 0 || index >= m_states.size())
        return;
    m_states[index].solo = !m_states[index].solo;
    rebuildStems();
}

QString SeparateController::suggestedMixName() const
{
    if (m_paths.isEmpty())
        return QString();
    QString base = QFileInfo(m_paths.first()).completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("_(drums|bass|other|vocals)$")));
    return QFileInfo(m_paths.first()).absolutePath() + QLatin1Char('/') + base
        + QStringLiteral("_mix.wav");
}

void SeparateController::exportMix(const QUrl &target)
{
    if (m_busy || m_paths.isEmpty())
        return;

    const QString output = target.toLocalFile();
    if (output.isEmpty()) {
        setError(tr("Choose a file to write the mix to."));
        return;
    }

    const QStringList paths = m_paths;
    const QVector<StemState> states = m_states;

    setError(QString());
    setMessage(tr("Rendering the mix…"));
    setBusy(true);

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, output] {
        const QString error = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!error.isEmpty()) {
            setMessage(QString());
            setError(error);
            return;
        }
        setMessage(tr("Mix written to %1").arg(QFileInfo(output).fileName()));
    });

    watcher->setFuture(QtConcurrent::run([paths, states, output]() -> QString {
        QString error;
        double gain = 1.0;
        double seconds = 0.0;
        if (!StemMixer::render(paths, states, output, &error, &gain, &seconds))
            return error;
        return QString();
    }));
}

void SeparateController::discover(const QUrl &input)
{
    const QString source = input.toLocalFile();
    if (source.isEmpty())
        return;

    for (const QVariant &item : m_stems) {
        if (item.toMap().value(QStringLiteral("path")).toString() == source)
            return; // the user is auditioning a stem, keep the strip
    }

    const QFileInfo info(source);
    const QString base = info.completeBaseName();
    const QString folder = info.absolutePath() + QLatin1Char('/') + base + QStringLiteral("_stems");

    QStringList paths;
    for (const QString &name : HtdemucsSeparator::stemNames()) {
        const QString path = folder + QLatin1Char('/') + base + QLatin1Char('_') + name
            + QStringLiteral(".wav");
        if (!QFileInfo::exists(path)) {
            if (!m_stems.isEmpty()) {
                m_stems.clear();
                m_folder.clear();
                emit stemsChanged();
            }
            return;
        }
        paths.append(path);
    }

    m_folder = folder;
    m_paths = paths;
    m_states = QVector<StemState>(paths.size());
    rebuildStems();
}

void SeparateController::run(const QUrl &input)
{
    if (m_busy)
        return;

    const QString source = input.toLocalFile();
    if (source.isEmpty()) {
        setError(tr("Only local files can be separated."));
        return;
    }

    QString modelPath;
    QString modelLabel;
    for (const QVariant &item : m_store->models()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("driver")).toString() != QLatin1String("htdemucs"))
            continue;
        if (map.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
            continue;
        modelPath = map.value(QStringLiteral("weights")).toString();
        modelLabel = map.value(QStringLiteral("name")).toString();
        break;
    }
    if (modelPath.isEmpty()) {
        setError(tr("No verified htdemucs model is installed. Run scripts/install-demucs.sh."));
        return;
    }

    const QFileInfo info(source);
    const QString folder = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
        + QStringLiteral("_stems");

    setError(QString());
    setMessage(tr("Separating with %1…").arg(modelLabel));
    setBusy(true);

    auto *watcher = new QFutureWatcher<JobResult>(this);
    connect(this, &SeparateController::progress, this, [this](int done, int total) {
        setMessage(tr("Separating… %1/%2").arg(done).arg(total));
    });
    connect(watcher, &QFutureWatcher<JobResult>::finished, this, [this, watcher] {
        const JobResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!result.ok) {
            setMessage(QString());
            setError(result.error);
            return;
        }

        m_folder = result.folder;
        m_paths = result.stems;
        m_states = QVector<StemState>(result.stems.size());
        rebuildStems();

        setMessage(tr("Separated %1 s in %2 s (%3× realtime) → %4")
                       .arg(result.audioSeconds, 0, 'f', 1)
                       .arg(result.wallSeconds, 0, 'f', 1)
                       .arg(result.wallSeconds > 0
                                ? result.audioSeconds / result.wallSeconds
                                : 0.0,
                            0, 'f', 2)
                       .arg(QFileInfo(result.folder).fileName()));
        emit finished(result.folder);
    });

    watcher->setFuture(QtConcurrent::run([this, source, folder, modelPath] {
        return runJob(source, folder, modelPath, [this](int done, int total) {
            emit progress(done, total);
        });
    }));
}

} // namespace stemhand
