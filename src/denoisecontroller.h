#pragma once

#include <atomic>

#include <QObject>
#include <QUrl>

class QFutureWatcherBase;

namespace stemhand {

class ModelStore;

// Runs the denoise engine off the GUI thread and reports back in terms the
// interface can render: seconds of audio, seconds of wall clock, realtime
// factor. Deliberately one job at a time — the engine is CPU-bound and a queue
// of parallel jobs would only make every one of them slower.
class DenoiseController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY finished)
    Q_PROPERTY(bool hasVoiceModel READ hasVoiceModel NOTIFY voiceModelChanged)

public:
    explicit DenoiseController(ModelStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString lastOutput() const { return m_lastOutput; }
    bool hasVoiceModel() const;

    // Writes "<name> (stemhand denoised).wav" next to the input.
    Q_INVOKABLE QString suggestedOutput(const QUrl &input) const;
    Q_INVOKABLE void run(const QUrl &input);
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void messageChanged();
    void errorChanged();
    void voiceModelChanged();
    void finished(const QString &outputPath);
    void progress(int done, int total);

private:
    void setBusy(bool busy);
    void setMessage(const QString &message);
    void setError(const QString &error);

    std::atomic_bool m_cancel{false};
    ModelStore *m_store = nullptr;
    bool m_busy = false;
    QString m_message;
    QString m_error;
    QString m_lastOutput;
};

} // namespace stemhand
