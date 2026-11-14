#include "updatecontroller.h"

#include "modelstore.h"
#include "registry.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace stemhand {
namespace {

struct UpdateResult {
    bool ok = false;
    QString error;
    QStringList installed;
    int upToDate = 0;
};

UpdateResult runUpdate(const QString &storeRoot, const QString &registryUrl)
{
    UpdateResult result;

    ModelStore store;
    store.setRoot(storeRoot);
    store.rescan();

    RegistryIndex index;
    if (!parseIndex(QUrl(registryUrl), &index, &result.error))
        return result;

    QList<UpdatePlan> plan;
    if (!planUpdates(index, &store, &plan, &result.error))
        return result;

    for (const UpdatePlan &item : plan) {
        QString installedDir;
        if (!installModel(item.manifestUrl, storeRoot, &installedDir, &result.error))
            return result;
        result.installed.append(QStringLiteral("%1 %2").arg(item.id, item.version));
    }

    result.upToDate = index.models.size() - result.installed.size();
    result.ok = true;
    return result;
}

} // namespace

UpdateController::UpdateController(ModelStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

QString UpdateController::registryUrl() const
{
    return readRegistryUrl(m_store->root());
}

void UpdateController::checkAndInstall()
{
    if (m_busy)
        return;

    const QString registry = registryUrl();
    if (registry.isEmpty()) {
        m_error = tr("No registry configured. Run: stemhand --update <index-url>");
        emit errorChanged();
        return;
    }

    const QString root = m_store->root();
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
    m_message = tr("Checking %1…").arg(registry);
    emit messageChanged();
    m_busy = true;
    emit busyChanged();

    auto *watcher = new QFutureWatcher<UpdateResult>(this);
    connect(watcher, &QFutureWatcher<UpdateResult>::finished, this, [this, watcher] {
        const UpdateResult result = watcher->result();
        watcher->deleteLater();

        m_busy = false;
        emit busyChanged();

        if (!result.ok) {
            m_message.clear();
            m_error = result.error;
            emit messageChanged();
            emit errorChanged();
            return;
        }

        if (result.installed.isEmpty()) {
            m_message = tr("All %1 models are up to date").arg(result.upToDate);
        } else {
            m_message = tr("Installed %1").arg(result.installed.join(QStringLiteral(", ")));
            m_store->rescan();
            emit installed();
        }
        emit messageChanged();
    });

    watcher->setFuture(QtConcurrent::run([root, registry] {
        return runUpdate(root, registry);
    }));
}

} // namespace stemhand
