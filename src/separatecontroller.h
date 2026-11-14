#pragma once

#include "engine/stemmixer.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace stemhand {

class ModelStore;

// Four-stem separation in the background. The engine reports per-chunk
// progress, which matters here: a four-minute song takes minutes, and a
// spinner with no numbers looks like a hang.
class SeparateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList stems READ stems NOTIFY stemsChanged)
    Q_PROPERTY(bool hasStemModel READ hasStemModel NOTIFY hasStemModelChanged)

public:
    explicit SeparateController(ModelStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QVariantList stems() const { return m_stems; }
    bool hasStemModel() const;

    Q_INVOKABLE void run(const QUrl &input);
    Q_INVOKABLE void setGain(int index, double decibels);
    Q_INVOKABLE void toggleMute(int index);
    Q_INVOKABLE void toggleSolo(int index);
    Q_INVOKABLE QString suggestedMixName() const;
    Q_INVOKABLE void exportMix(const QUrl &target);
    // Shows an existing <name>_stems folder when a file with stems is opened.
    Q_INVOKABLE void discover(const QUrl &input);
    Q_INVOKABLE void openStemsFolder() const;

signals:
    void busyChanged();
    void messageChanged();
    void errorChanged();
    void stemsChanged();
    void hasStemModelChanged();
    void progress(int done, int total);
    void finished(const QString &folder);

private:
    void rebuildStems();
    void setBusy(bool busy);
    void setMessage(const QString &message);
    void setError(const QString &error);

    ModelStore *m_store = nullptr;
    bool m_busy = false;
    QString m_message;
    QString m_error;
    QVariantList m_stems;
    QStringList m_paths;
    QVector<StemState> m_states;
    QString m_folder;
};

} // namespace stemhand
