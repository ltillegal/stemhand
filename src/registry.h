#pragma once

#include "manifest.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

namespace photone {

class ModelStore;

// A registry index is a tiny signed-pointer document: it lists model ids,
// versions and where their manifest lives. Everything that matters — weights
// hashes, licences, engine ABI — stays in the manifest, which is fetched and
// verified per model. The index therefore cannot lie about content; it can only
// point, and a wrong pointer fails the signature check.
struct RegistryEntry {
    QString id;
    QString version;
    QUrl manifestUrl;
};

struct RegistryIndex {
    static constexpr int kSchema = 1;
    int schema = 0;
    QList<RegistryEntry> models;

    static bool parse(const QByteArray &json, RegistryIndex *index, QString *error);
};

struct UpdatePlan {
    QString id;
    QString version;
    QUrl manifestUrl;
    QString installedVersion; // empty when nothing is installed
    bool isUpgrade = false;   // something older is installed
};

// Blocking network helpers: the updater runs on a worker thread, and the CLI
// wants a straight-line script. Both file:// and http(s) work, which is what
// makes the update path testable without a server.
bool fetchUrl(const QUrl &url, QByteArray *payload, QString *error);

bool parseIndex(const QUrl &indexUrl, RegistryIndex *index, QString *error);

// Compares dotted versions numerically ("1.10.0" > "1.9.0").
bool versionLess(const QString &left, const QString &right);
bool versionEqual(const QString &left, const QString &right);

// Plans what the index offers against what the store already has.
bool planUpdates(const RegistryIndex &index, ModelStore *store, QList<UpdatePlan> *plan,
                 QString *error);

// Downloads, verifies and atomically installs one model version.
// On success `installedDir` is the final directory inside the store.
bool installModel(const QUrl &manifestUrl, const QString &storeRoot, QString *installedDir,
                  QString *error);

// The registry URL is remembered next to the models, not in a global config:
// a store stays self-describing, and tests can point at a fixture directory.
QString readRegistryUrl(const QString &storeRoot);
void writeRegistryUrl(const QString &storeRoot, const QUrl &url);

} // namespace photone
