#include "denoisecontroller.h"
#include "modelstore.h"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QtTest>

using namespace photone;

// Exercises the asynchronous path the GUI actually uses: a worker thread, the
// engine, the output file, and the finished signal carrying its path.
class DenoiseControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void processesVoiceModel();
};

void DenoiseControllerTest::processesVoiceModel()
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

QTEST_GUILESS_MAIN(DenoiseControllerTest)

#include "denoise_test.moc"
