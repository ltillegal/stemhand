#include "manifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>

namespace photone {
namespace {

void fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

} // namespace

std::optional<ModelManifest> ModelManifest::parse(const QByteArray &json, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, QStringLiteral("manifest is not valid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        fail(error, QStringLiteral("manifest root must be a JSON object"));
        return std::nullopt;
    }

    const QJsonObject object = doc.object();
    ModelManifest manifest;

    manifest.schema = object.value(QStringLiteral("schema")).toInt(0);
    if (manifest.schema != kSchema) {
        fail(error, QStringLiteral("unsupported manifest schema %1 (expected %2)")
                        .arg(manifest.schema)
                        .arg(kSchema));
        return std::nullopt;
    }

    manifest.id = object.value(QStringLiteral("id")).toString().trimmed();
    manifest.version = object.value(QStringLiteral("version")).toString().trimmed();
    manifest.name = object.value(QStringLiteral("name")).toString().trimmed();
    manifest.kind = object.value(QStringLiteral("kind")).toString().trimmed();
    manifest.driver = object.value(QStringLiteral("driver")).toString().trimmed();
    manifest.engineAbi = object.value(QStringLiteral("engine_abi")).toInt(0);
    manifest.sampleRate = object.value(QStringLiteral("sample_rate")).toInt(0);
    manifest.channels = object.value(QStringLiteral("channels")).toInt(0);
    manifest.minEngine = object.value(QStringLiteral("min_engine")).toString();
    manifest.homepage = object.value(QStringLiteral("homepage")).toString();

    const QJsonObject license = object.value(QStringLiteral("license")).toObject();
    manifest.codeLicense = license.value(QStringLiteral("code")).toString();
    manifest.weightsLicense = license.value(QStringLiteral("weights")).toString();
    manifest.commercialOk = license.value(QStringLiteral("commercial")).toBool(false);

    for (const QJsonValue &value : object.value(QStringLiteral("stems")).toArray())
        manifest.stems.append(value.toString());
    for (const QJsonValue &value : object.value(QStringLiteral("eps")).toArray())
        manifest.eps.append(value.toString());

    if (manifest.id.isEmpty()) {
        fail(error, QStringLiteral("manifest has no id"));
        return std::nullopt;
    }
    if (manifest.version.isEmpty()) {
        fail(error, QStringLiteral("manifest has no version"));
        return std::nullopt;
    }
    if (manifest.name.isEmpty())
        manifest.name = manifest.id;

    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        fail(error, QStringLiteral("manifest lists no files"));
        return std::nullopt;
    }

    static const QRegularExpression hex256(QStringLiteral("^[0-9a-fA-F]{64}$"));
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        ModelFile entry;
        entry.path = file.value(QStringLiteral("path")).toString();
        entry.sha256 = file.value(QStringLiteral("sha256")).toString().toLower();
        entry.size = static_cast<qint64>(file.value(QStringLiteral("size")).toDouble(-1));

        if (!isSafeRelativePath(entry.path)) {
            fail(error, QStringLiteral("unsafe or empty file path in manifest: %1").arg(entry.path));
            return std::nullopt;
        }
        if (!hex256.match(entry.sha256).hasMatch()) {
            fail(error, QStringLiteral("file %1 has no valid sha256").arg(entry.path));
            return std::nullopt;
        }
        if (entry.size < 0) {
            fail(error, QStringLiteral("file %1 has no valid size").arg(entry.path));
            return std::nullopt;
        }
        manifest.files.append(entry);
    }

    return manifest;
}

} // namespace photone
