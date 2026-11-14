#pragma once

#include <QObject>

namespace photone {

class ModelStore;

// "Update models" in the app: reads the registry URL remembered in the store,
// plans against it, installs what is missing, then rescans so the list and the
// denoise button pick the new version up immediately.
class UpdateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString registryUrl READ registryUrl NOTIFY registryChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY registryChanged)

public:
    explicit UpdateController(ModelStore *store, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString registryUrl() const;
    bool canCheck() const { return !registryUrl().isEmpty(); }

    Q_INVOKABLE void checkAndInstall();

signals:
    void busyChanged();
    void messageChanged();
    void errorChanged();
    void registryChanged();
    void installed();

private:
    ModelStore *m_store = nullptr;
    bool m_busy = false;
    QString m_message;
    QString m_error;
};

} // namespace photone
