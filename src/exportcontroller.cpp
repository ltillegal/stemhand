#include "exportcontroller.h"

#include "engine/audioio.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace stemhand {
namespace {

QString errorLine(const QByteArray &output)
{
    const QString text = QString::fromUtf8(output).trimmed();
    const qsizetype newline = text.indexOf(QLatin1Char('\n'));
    return (newline < 0 ? text : text.left(newline)).trimmed().left(200);
}

bool convert(const QString &source, const QString &target, QString *error)
{
    if (!QFileInfo::exists(source)) {
        *error = QObject::tr("The source file is gone: %1").arg(source);
        return false;
    }

    const QString ffmpeg = findTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        *error = QObject::tr("ffmpeg is not available.");
        return false;
    }

    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                           QStringLiteral("-i"), source, target});
    if (!process.waitForStarted(5000)) {
        *error = QObject::tr("ffmpeg did not start.");
        return false;
    }
    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = errorLine(process.readAllStandardError());
        if (error->isEmpty())
            *error = QObject::tr("ffmpeg could not write %1").arg(target);
        return false;
    }
    return true;
}

} // namespace

ExportController::ExportController(QObject *parent)
    : QObject(parent)
{
}

void ExportController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ExportController::setMessage(const QString &message)
{
    if (m_message == message)
        return;
    m_message = message;
    emit messageChanged();
}

void ExportController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void ExportController::keepSource(const QUrl &source)
{
    const QString path = source.toLocalFile();
    if (path == m_source)
        return;
    m_source = path;

    const QFileInfo info(path);
    QString name = info.completeBaseName();
    // Do not grow the name on every export: "x (stemhand denoised) (stemhand
    // denoised)" is a real thing that happens when the suggestion is derived
    // from an already exported file.
    name.remove(QRegularExpression(QStringLiteral(" \\(stemhand denoised\\)$")));
    m_suggestedName = info.absolutePath() + QLatin1Char('/') + name + QStringLiteral(".wav");
    emit suggestionChanged();
}

void ExportController::exportFile(const QUrl &target)
{
    if (m_busy || m_source.isEmpty())
        return;

    const QString destination = target.toLocalFile();
    if (destination.isEmpty()) {
        setError(tr("Choose where to save the file."));
        return;
    }
    if (QFileInfo(destination).absoluteFilePath() == QFileInfo(m_source).absoluteFilePath()) {
        setError(tr("The source and the destination are the same file."));
        return;
    }

    const QString source = m_source;
    setError(QString());
    setMessage(tr("Exporting…"));
    setBusy(true);

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, destination] {
        const QString error = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!error.isEmpty()) {
            setMessage(QString());
            setError(error);
            return;
        }
        setMessage(tr("Saved %1").arg(QFileInfo(destination).fileName()));
    });

    watcher->setFuture(QtConcurrent::run([source, destination]() -> QString {
        QString error;
        if (!convert(source, destination, &error))
            return error;
        return QString();
    }));
}

} // namespace stemhand
