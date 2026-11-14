#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QUrl>

namespace stemhand {

// Reads what the UI needs to draw a file: metadata via ffprobe, and a
// downsampled peak envelope via ffmpeg. Decoding at 8 kHz mono is ~100x
// cheaper than the full-rate decode and is all a waveform needs.
class AudioFile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(double duration READ duration NOTIFY metadataChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY metadataChanged)
    Q_PROPERTY(int channels READ channels NOTIFY metadataChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY metadataChanged)
    Q_PROPERTY(QString formatLabel READ formatLabel NOTIFY metadataChanged)
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY peaksChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY fileChanged)

public:
    explicit AudioFile(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString fileName() const;
    double duration() const { return m_duration; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    QString codec() const { return m_codec; }
    QString formatLabel() const;
    QVariantList peaks() const { return m_peaks; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool hasAudio() const { return !m_path.isEmpty(); }

    Q_INVOKABLE void load(const QUrl &url);
    Q_INVOKABLE void clear();

signals:
    void fileChanged();
    void metadataChanged();
    void peaksChanged();
    void loadingChanged();
    void errorChanged();

private:
    void probe(const QString &path);
    void startDecode(const QString &path);
    void finishDecode();
    void setLoading(bool loading);
    void setError(const QString &message);

    QString m_path;
    double m_duration = 0.0;
    int m_sampleRate = 0;
    int m_channels = 0;
    QString m_codec;
    QVariantList m_peaks;
    bool m_loading = false;
    QString m_error;

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
};

} // namespace stemhand
