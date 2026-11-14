#include "denoisecontroller.h"
#include "modelstore.h"
#include "registry.h"
#include "updatecontroller.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace photone;

// Exercises the same asynchronous paths the GUI uses: worker thread, network
// (file:// here), verification, and the signal the interface reacts to.
class ControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void denoisesVoice();
    void installsFromRegistry();
};

void ControllersTest::denoisesVoice()
{
    const QString storeRoot = qEnvironmentVariable("PHOTONE_TEST_MODELS");
    const QString input = qEnvironmentVariable("PHOTONE_TEST_INPUT");
    if (storeRoot.isEmpty() || input.isEmpty())
        QSKIP("set PHOTONE_TEST_MODELS and PHOTONE_TEST_INPUT to run this test");

    ModelStore store;
    store.setRoot(storeRoot);
    store.rescan();

    DenoiseController controller(&store);
    QVERIFY(controller.hasVoiceModel());

    QSignalSpy finished(&controller, &DenoiseController::finished);
    QSignalSpy errors(&controller, &DenoiseController::errorChanged);

    controller.run(QUrl::fromLocalFile(input));
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 120000);

    QCOMPARE(errors.count(), 0);
    const QString output = finished.first().first().toString();
    QVERIFY2(QFileInfo::exists(output), qPrintable(output));
    QVERIFY(QFileInfo(output).size() > 4096);
    QVERIFY(!controller.busy());
    QVERIFY(controller.message().contains(QStringLiteral("realtime")));
}

void ControllersTest::installsFromRegistry()
{
    const QString registry = qEnvironmentVariable("PHOTONE_TEST_REGISTRY");
    if (registry.isEmpty())
        QSKIP("set PHOTONE_TEST_REGISTRY to run this test");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ModelStore store;
    store.setRoot(temporary.path() + QStringLiteral("/models"));
    store.rescan();

    writeRegistryUrl(store.root(), QUrl::fromLocalFile(registry));

    UpdateController controller(&store);
    QVERIFY(controller.canCheck());

    QSignalSpy installed(&controller, &UpdateController::installed);
    QSignalSpy errors(&controller, &UpdateController::errorChanged);

    controller.checkAndInstall();
    QVERIFY(controller.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 60000);

    QVERIFY2(errors.count() == 0, qPrintable(controller.error()));
    QCOMPARE(installed.count(), 1);

    store.rescan();
    QCOMPARE(store.models().size(), 1);
    QCOMPARE(store.models().first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("ok"));
    QCOMPARE(store.models().first().toMap().value(QStringLiteral("version")).toString(),
             QStringLiteral("1.1.0"));
}

QTEST_GUILESS_MAIN(ControllersTest)

#include "controllers_test.moc"
