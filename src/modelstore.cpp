#include "modelstore.h"

#include "keys.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace photone {
namespace {

constexpr qint64 kMaxManifestBytes = 4 * 1024 * 1024;

QString humanBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 0);
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', mb < 10 ? 1 : 0);
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
}

QString kindLabel(const QString &kind)
{
    if (kind == QLatin1String("music4"))
        return QStringLiteral("music · 4 stems");
    if (kind == QLatin1String("drums5"))
        return QStringLiteral("drums · 5 stems");
    if (kind == QLatin1String("voice"))
        return QStringLiteral("voice");
    if (kind == QLatin1String("denoise"))
        return QStringLiteral("denoise");
    if (kind == QLatin1String("enhance"))
        return QStringLiteral("restore");
    return kind;
}

} // namespace

QString ModelEntry::statusKey() const
{
    switch (status) {
    case ModelStatus::Ok:
        return QStringLiteral("ok");
    case ModelStatus::Unverified:
        return QStringLiteral("unverified");
    case ModelStatus::Incompatible:
        return QStringLiteral("incompatible");
    case ModelStatus::Corrupt:
        return QStringLiteral("corrupt");
    case ModelStatus::Broken:
        return QStringLiteral("broken");
    }
    return QStringLiteral("broken");
}

ModelStore::ModelStore(QObject *parent)
    : QObject(parent)
    , m_root(defaultRoot())
{
}

QString ModelStore::defaultRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/models");
}

QVariantList ModelStore::models() const
{
    QVariantList list;
    list.reserve(m_entries.size());
    for (const ModelEntry &entry : m_entries) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), entry.manifest.id);
        map.insert(QStringLiteral("version"), entry.manifest.version);
        map.insert(QStringLiteral("name"), entry.manifest.name);
        map.insert(QStringLiteral("kind"), entry.manifest.kind);
        map.insert(QStringLiteral("kindLabel"), kindLabel(entry.manifest.kind));
        map.insert(QStringLiteral("stems"), entry.manifest.stems);
        map.insert(QStringLiteral("sampleRate"), entry.manifest.sampleRate);
        map.insert(QStringLiteral("license"), entry.manifest.weightsLicense);
        map.insert(QStringLiteral("commercial"), entry.manifest.commercialOk);
        map.insert(QStringLiteral("status"), entry.statusKey());
        map.insert(QStringLiteral("detail"), entry.detail);
        map.insert(QStringLiteral("bytes"), entry.bytes);
        map.insert(QStringLiteral("sizeLabel"), humanBytes(entry.bytes));
        map.insert(QStringLiteral("dir"), entry.dir);
        map.insert(QStringLiteral("signed"), entry.signaturePresent);
        list.append(map);
    }
    return list;
}

QString ModelStore::root() const
{
    return m_root;
}

QString ModelStore::rootDisplay() const
{
    return m_root;
}

QString ModelStore::summary() const
{
    if (m_entries.isEmpty())
        return QStringLiteral("no models installed");

    int ok = 0;
    int flagged = 0;
    qint64 bytes = 0;
    for (const ModelEntry &entry : m_entries) {
        bytes += entry.bytes;
        if (entry.status == ModelStatus::Ok)
            ++ok;
        else if (entry.status != ModelStatus::Unverified)
            ++flagged;
    }

    QString text = QStringLiteral("%1 model%2 · %3 verified · %4")
                       .arg(m_entries.size())
                       .arg(m_entries.size() == 1 ? QString() : QStringLiteral("s"))
                       .arg(ok)
                       .arg(humanBytes(bytes));
    if (flagged > 0)
        text += QStringLiteral(" · %1 need attention").arg(flagged);
    return text;
}

void ModelStore::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}

void ModelStore::setRoot(const QString &root)
{
    m_root = root;
    emit changed();
}

void ModelStore::rescan()
{
    m_entries.clear();

    // Model ids may contain slashes ("denoise/deepfilternet3"), so the version
    // directory is defined by where manifest.json sits, not by a fixed depth.
    QDirIterator it(m_root, {QStringLiteral("manifest.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        loadEntry(it.fileInfo().absolutePath());
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const ModelEntry &a, const ModelEntry &b) {
        if (a.manifest.kind != b.manifest.kind)
            return a.manifest.kind < b.manifest.kind;
        return a.manifest.id < b.manifest.id;
    });

    emit changed();
}

void ModelStore::loadEntry(const QString &versionDir)
{
    ModelEntry entry;
    entry.dir = versionDir;

    QFile manifestFile(versionDir + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        entry.status = ModelStatus::Broken;
        entry.detail = QStringLiteral("manifest.json is missing");
        entry.manifest.name = QFileInfo(versionDir).fileName();
        m_entries.append(entry);
        return;
    }
    if (manifestFile.size() > kMaxManifestBytes) {
        entry.status = ModelStatus::Broken;
        entry.detail = QStringLiteral("manifest.json is unreasonably large");
        entry.manifest.name = QFileInfo(versionDir).fileName();
        m_entries.append(entry);
        return;
    }
    const QByteArray manifestBytes = manifestFile.readAll();

    QString parseError;
    auto manifest = ModelManifest::parse(manifestBytes, &parseError);
    if (!manifest) {
        entry.status = ModelStatus::Broken;
        entry.detail = parseError;
        entry.manifest.name = QFileInfo(versionDir).fileName();
        m_entries.append(entry);
        return;
    }
    entry.manifest = *manifest;

    QFile signatureFile(versionDir + QStringLiteral("/manifest.sig"));
    if (signatureFile.open(QIODevice::ReadOnly)) {
        entry.signaturePresent = true;
        const QByteArray signature = QByteArray::fromBase64(signatureFile.readAll().trimmed());
        if (!verifyDetached(manifestBytes, signature, pinnedKeyHex())) {
            entry.status = ModelStatus::Corrupt;
            entry.detail = QStringLiteral("manifest signature does not verify against the built-in key");
            m_entries.append(entry);
            return;
        }
    }

    if (entry.manifest.engineAbi != PHOTONE_MODEL_ENGINE_ABI) {
        entry.status = ModelStatus::Incompatible;
        entry.detail = QStringLiteral("model needs engine ABI %1, this build is %2")
                           .arg(entry.manifest.engineAbi)
                           .arg(PHOTONE_MODEL_ENGINE_ABI);
        m_entries.append(entry);
        return;
    }

    for (const ModelFile &file : entry.manifest.files) {
        const QString path = versionDir + QLatin1Char('/') + file.path;
        const QFileInfo info(path);
        if (!info.exists()) {
            entry.status = ModelStatus::Corrupt;
            entry.detail = QStringLiteral("missing file: %1").arg(file.path);
            m_entries.append(entry);
            return;
        }
        if (info.size() != file.size) {
            entry.status = ModelStatus::Corrupt;
            entry.detail = QStringLiteral("size mismatch on %1").arg(file.path);
            m_entries.append(entry);
            return;
        }
        entry.bytes += info.size();
        if (sha256File(path) != file.sha256) {
            entry.status = ModelStatus::Corrupt;
            entry.detail = QStringLiteral("sha256 mismatch on %1").arg(file.path);
            m_entries.append(entry);
            return;
        }
    }

    if (!entry.signaturePresent) {
        entry.status = ModelStatus::Unverified;
        entry.detail = QStringLiteral("no manifest.sig — integrity checked, origin unknown");
    } else {
        entry.status = ModelStatus::Ok;
        entry.detail = QStringLiteral("signature verified");
    }

    m_entries.append(entry);
}

bool ModelStore::importFolder(const QUrl &folder)
{
    const QString source = folder.toLocalFile();
    if (source.isEmpty()) {
        setLastError(QStringLiteral("only local folders can be imported"));
        return false;
    }

    QFile manifestFile(source + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("that folder has no manifest.json"));
        return false;
    }
    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QString parseError;
    auto manifest = ModelManifest::parse(manifestBytes, &parseError);
    if (!manifest) {
        setLastError(parseError);
        return false;
    }

    const QString target = m_root + QLatin1Char('/') + manifest->id + QLatin1Char('/')
        + manifest->version;
    if (QFileInfo::exists(target)) {
        setLastError(QStringLiteral("%1 %2 is already installed")
                         .arg(manifest->id, manifest->version));
        return false;
    }

    if (!QDir().mkpath(target)) {
        setLastError(QStringLiteral("cannot create %1").arg(target));
        return false;
    }

    QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString relative = QDir(source).relativeFilePath(from);
        const QString to = target + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(from, to)) {
            setLastError(QStringLiteral("cannot copy %1").arg(relative));
            return false;
        }
    }

    rescan();
    setLastError(QString());
    return true;
}

void ModelStore::openRootInFileManager()
{
    QDir().mkpath(m_root);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));
}

} // namespace photone
