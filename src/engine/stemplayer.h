#pragma once

#include "stemmixer.h"

#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>

namespace stemhand {

class StemPlayerPrivate;

// Real-time playback of the stem mix.
//
// Mute, solo and gain are applied per buffer, so changes are audible while the
// mix plays — rendering a file to hear a slider move would be both slower and
// pointless. The audio thread only reads preallocated buffers and atomics; no
// allocation, no locking, no file metadata work happens in readData().
class StemPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY loadedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY loadedChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit StemPlayer(QObject *parent = nullptr);
    ~StemPlayer() override;

    bool playing() const { return m_playing; }
    bool hasAudio() const { return m_duration > 0.0; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QString positionText() const;
    QString durationText() const;
    QString error() const { return m_error; }

    bool load(const QStringList &paths, const QVector<StemState> &states);
    void setStates(const QVector<StemState> &states);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();

signals:
    void playingChanged();
    void loadedChanged();
    void positionChanged();
    void errorChanged();
    void finished();

private:
    void setError(const QString &error);
    void setPlaying(bool playing);

    std::unique_ptr<StemPlayerPrivate> d;
    bool m_playing = false;
    double m_position = 0.0;
    double m_duration = 0.0;
    QString m_error;
};

} // namespace stemhand
