#include "registry.h"

#include "keys.h"
#include "modelstore.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace photone {
namespace {

constexpr int kFetchTimeoutMs = 60000;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString randomSuffix()
{
    return QStringLiteral("%1").arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
}

// Downloads into `payload`. file:// is read directly: QNetworkAccessManager can
// handle it, but a plain QFile is synchronous, obvious and has no redirects.
bool fetchLocal(const QString &path, QByteArray *payload, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot read %1").arg(path));
    *payload = file.readAll();
    return true;
}

void removeTree(const QString &path)
{
    if (path.isEmpty())
        return;
    QDir(path).removeRecursively();
}

} // namespace

bool fetchUrl(const QUrl &url, QByteArray *payload, QString *error)
{
    if (!url.isValid())
        return fail(error, QStringLiteral("invalid URL: %1").arg(url.toString()));

    if (url.isLocalFile())
        return fetchLocal(url.toLocalFile(), payload, error);

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("photone/0.1"));

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(kFetchTimeoutMs);

    bool timedOut = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    timeout.start();
    loop.exec();

    const bool ok = !timedOut && reply->error() == QNetworkReply::NoError;
    if (ok)
        *payload = reply->readAll();
    else
        fail(error, QStringLiteral("download failed for %1: %2")
                        .arg(url.toString(),
                             timedOut ? QStringLiteral("timed out")
                                      : reply->errorString()));
    reply->deleteLater();
    return ok;
}

bool RegistryIndex::parse(const QByteArray &json, RegistryIndex *index, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("index is not JSON: %1").arg(parseError.errorString()));
    if (!doc.isObject())
        return fail(error, QStringLiteral("index root must be an object"));

    const QJsonObject object = doc.object();
    index->schema = object.value(QStringLiteral("schema")).toInt(0);
    if (index->schema != kSchema)
        return fail(error, QStringLiteral("unsupported index schema %1").arg(index->schema));

    const QJsonArray models = object.value(QStringLiteral("models")).toArray();
    if (models.isEmpty())
        return fail(error, QStringLiteral("index lists no models"));

    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        RegistryEntry entry;
        entry.id = model.value(QStringLiteral("id")).toString().trimmed();
        entry.version = model.value(QStringLiteral("version")).toString().trimmed();
        entry.manifestUrl = QUrl(model.value(QStringLiteral("manifest")).toString());

        if (entry.id.isEmpty() || entry.version.isEmpty() || !entry.manifestUrl.isValid())
            return fail(error, QStringLiteral("index entry is missing id, version or manifest"));
        index->models.append(entry);
    }
    return true;
}

bool versionLess(const QString &left, const QString &right)
{
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const qsizetype count = std::max(a.size(), b.size());
    for (qsizetype i = 0; i < count; ++i) {
        const int x = i < a.size() ? a.at(i).toInt() : 0;
        const int y = i < b.size() ? b.at(i).toInt() : 0;
        if (x != y)
            return x < y;
    }
    return false;
}

bool versionEqual(const QString &left, const QString &right)
{
    return !versionLess(left, right) && !versionLess(right, left);
}

bool parseIndex(const QUrl &indexUrl, RegistryIndex *index, QString *error)
{
    QByteArray payload;
    if (!fetchUrl(indexUrl, &payload, error))
        return false;
    if (!RegistryIndex::parse(payload, index, error))
        return false;

    // Index entries normally point relatively at their manifests; resolve them
    // against the index location so the rest of the updater only sees absolute
    // URLs. This is what lets the same registry work from file:// and https://.
    for (RegistryEntry &entry : index->models) {
        if (entry.manifestUrl.isRelative())
            entry.manifestUrl = indexUrl.resolved(entry.manifestUrl);
    }
    return true;
}

bool planUpdates(const RegistryIndex &index, ModelStore *store, QList<UpdatePlan> *plan,
                 QString *error)
{
    Q_UNUSED(error)
    QHash<QString, QString> installed;
    for (const QVariant &item : store->models()) {
        const QVariantMap map = item.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        const QString version = map.value(QStringLiteral("version")).toString();
        // Only a healthy model counts as installed; a corrupt one is
        // reinstalled rather than treated as current.
        if (map.value(QStringLiteral("status")).toString() == QLatin1String("corrupt")
            || map.value(QStringLiteral("status")).toString() == QLatin1String("broken"))
            continue;
        if (!installed.contains(id) || versionLess(installed.value(id), version))
            installed.insert(id, version);
    }

    for (const RegistryEntry &entry : index.models) {
        const QString current = installed.value(entry.id);
        if (!current.isEmpty() && !versionLess(current, entry.version))
            continue;

        UpdatePlan item;
        item.id = entry.id;
        item.version = entry.version;
        item.manifestUrl = entry.manifestUrl;
        item.installedVersion = current;
        item.isUpgrade = !current.isEmpty();
        plan->append(item);
    }
    return true;
}

bool installModel(const QUrl &manifestUrl, const QString &storeRoot, QString *installedDir,
                  QString *error)
{
    QByteArray manifestBytes;
    if (!fetchUrl(manifestUrl, &manifestBytes, error))
        return false;

    QByteArray signatureBytes;
    QUrl signatureUrl = manifestUrl;
    signatureUrl.setPath(manifestUrl.path() + QStringLiteral(".sig"));
    if (!fetchUrl(signatureUrl, &signatureBytes, error)) {
        if (error)
            *error = QStringLiteral("no signature next to %1: %2")
                         .arg(manifestUrl.toString(), *error);
        return false;
    }

    const QByteArray signature = QByteArray::fromBase64(signatureBytes.trimmed());
    if (!verifyDetached(manifestBytes, signature, pinnedKeyHex()))
        return fail(error, QStringLiteral("signature does not verify: %1").arg(manifestUrl.toString()));

    QString parseError;
    auto manifest = ModelManifest::parse(manifestBytes, &parseError);
    if (!manifest)
        return fail(error, parseError);

    const QString target = storeRoot + QLatin1Char('/') + manifest->id + QLatin1Char('/')
        + manifest->version;
    if (QFileInfo::exists(target))
        return fail(error, QStringLiteral("%1 %2 is already installed")
                               .arg(manifest->id, manifest->version));

    // Stage in a temp directory inside the store, then rename. A crash leaves
    // junk that the next install sweeps away, never a half-installed model.
    const QString incoming = storeRoot + QStringLiteral("/.incoming");
    if (!QDir().mkpath(incoming))
        return fail(error, QStringLiteral("cannot create %1").arg(incoming));

    const QString staging = incoming + QLatin1Char('/') + randomSuffix();
    if (!QDir().mkpath(staging))
        return fail(error, QStringLiteral("cannot create %1").arg(staging));

    auto cleanup = [&] { removeTree(staging); };

    for (const ModelFile &file : manifest->files) {
        const QUrl fileUrl = manifestUrl.resolved(QUrl(file.path));
        QByteArray payload;
        if (!fetchUrl(fileUrl, &payload, error)) {
            cleanup();
            return false;
        }
        if (payload.size() != file.size) {
            cleanup();
            return fail(error, QStringLiteral("%1: expected %2 bytes, got %3")
                                   .arg(file.path)
                                   .arg(file.size)
                                   .arg(payload.size()));
        }

        const QString destination = staging + QLatin1Char('/') + file.path;
        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile out(destination);
        if (!out.open(QIODevice::WriteOnly)) {
            cleanup();
            return fail(error, QStringLiteral("cannot write %1").arg(destination));
        }
        out.write(payload);
        out.close();

        if (sha256File(destination).compare(file.sha256, Qt::CaseInsensitive) != 0) {
            cleanup();
            return fail(error, QStringLiteral("%1: sha256 mismatch").arg(file.path));
        }
    }

    QFile manifestOut(staging + QStringLiteral("/manifest.json"));
    if (!manifestOut.open(QIODevice::WriteOnly)) {
        cleanup();
        return fail(error, QStringLiteral("cannot write manifest.json"));
    }
    manifestOut.write(manifestBytes);
    manifestOut.close();

    QFile signatureOut(staging + QStringLiteral("/manifest.json.sig"));
    if (!signatureOut.open(QIODevice::WriteOnly)) {
        cleanup();
        return fail(error, QStringLiteral("cannot write manifest.json.sig"));
    }
    signatureOut.write(signatureBytes.trimmed() + "\n");
    signatureOut.close();

    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QDir().rename(staging, target)) {
        cleanup();
        return fail(error, QStringLiteral("cannot move the staged model into %1").arg(target));
    }

    if (installedDir)
        *installedDir = target;
    return true;
}

QString readRegistryUrl(const QString &storeRoot)
{
    QFile file(storeRoot + QStringLiteral("/registry.url"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

void writeRegistryUrl(const QString &storeRoot, const QUrl &url)
{
    QDir().mkpath(storeRoot);
    QFile file(storeRoot + QStringLiteral("/registry.url"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(url.toString().toUtf8() + "\n");
}

} // namespace photone
