#pragma once

#include "manifest.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace photone {

enum class ModelStatus {
    Ok,           // signature valid, all files match, ABI compatible
    Unverified,   // files match, but no signature from our key
    Incompatible, // engine ABI or schema does not match this build
    Corrupt,      // a file is missing or its hash differs
    Broken        // manifest missing or unparseable
};

struct ModelEntry {
    QString dir;
    ModelManifest manifest;
    ModelStatus status = ModelStatus::Broken;
    QString detail;
    qint64 bytes = 0;
    bool signaturePresent = false;

    QString statusKey() const;
};

// On-disk model store.
//
// Layout: <root>/<id>/<version>/{manifest.json, manifest.sig, ...weights}
//
// Versions coexist; nothing is overwritten in place, so a rollback is just a
// matter of pointing at another version directory. The rescan is cheap enough
// to run on every window focus.
class ModelStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
    Q_PROPERTY(QString root READ root NOTIFY changed)
    Q_PROPERTY(QString rootDisplay READ rootDisplay NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ModelStore(QObject *parent = nullptr);

    static QString defaultRoot();

    QVariantList models() const;
    QString root() const;
    QString rootDisplay() const;
    QString summary() const;
    QString lastError() const { return m_lastError; }

    void setRoot(const QString &root);

    Q_INVOKABLE void rescan();
    Q_INVOKABLE bool importFolder(const QUrl &folder);
    Q_INVOKABLE void openRootInFileManager();

signals:
    void changed();
    void lastErrorChanged();

private:
    void loadEntry(const QString &versionDir);
    void setLastError(const QString &message);

    QString m_root;
    QList<ModelEntry> m_entries;
    QString m_lastError;
};

} // namespace photone
