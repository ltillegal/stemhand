#pragma once

#include <QObject>
#include <QUrl>

namespace stemhand {

// Saves the currently opened audio — a denoised result, a stem, a mix — to
// wherever the user wants it. The extension picks the format; ffmpeg does the
// conversion, so nothing here needs a codec table.
class ExportController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY suggestionChanged)

public:
    explicit ExportController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString suggestedName() const { return m_suggestedName; }

    Q_INVOKABLE void keepSource(const QUrl &source);
    Q_INVOKABLE void exportFile(const QUrl &target);

signals:
    void busyChanged();
    void messageChanged();
    void errorChanged();
    void suggestionChanged();

private:
    void setBusy(bool busy);
    void setMessage(const QString &message);
    void setError(const QString &error);

    QString m_source;
    QString m_suggestedName;
    QString m_message;
    QString m_error;
    bool m_busy = false;
};

} // namespace stemhand
