#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <atomic>

class QTimer;

namespace stemhand {

class ModelStore;

// A queue of files run through one operation.
//
// The queue is deliberately dumb: files are added, the user presses Start, and
// each row reports pending / running / ok / failed. A failed file does not stop
// the queue, because coming back to a folder of results is the whole point.
class BatchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString operation READ operation WRITE setOperation NOTIFY operationChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY hasModelChanged)

public:
    explicit BatchController(ModelStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString operation() const { return m_operation; }
    QVariantList jobs() const { return m_jobsVariant; }
    bool hasModel() const;

    void setOperation(const QString &operation);

    Q_INVOKABLE void addFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void messageChanged();
    void errorChanged();
    void operationChanged();
    void jobsChanged();
    void hasModelChanged();

private:
    struct Job {
        QString path;
        QString status; // pending | running | ok | failed
        QString detail;
    };

    void rebuildJobs();
    void setBusy(bool busy);
    void setMessage(const QString &message);
    void setError(const QString &error);

    ModelStore *m_store = nullptr;
    QList<Job> m_jobs;
    QVariantList m_jobsVariant;
    QString m_operation = QStringLiteral("denoise");
    QString m_message;
    QString m_error;
    bool m_busy = false;
    std::atomic_bool m_cancel{false};
};

} // namespace stemhand
