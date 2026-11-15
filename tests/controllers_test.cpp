#include "denoisecontroller.h"
#include "engine/stemmixer.h"
#include "engine/stemplayer.h"
#include "engine/wavreader.h"
#include "engine/wavwriter.h"
#include "modelstore.h"
#include "separatecontroller.h"
#include "registry.h"
#include "updatecontroller.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace stemhand;

// Exercises the same asynchronous paths the GUI uses: worker thread, network
// (file:// here), verification, and the signal the interface reacts to.
class ControllersTest : public QObject
{
    Q_OBJECT

private slots:
    void denoisesVoice();
    void mixesStems();
    void cancelsSeparation();
    void cancelsDenoising();
    void playsStems();
    void remembersMixState();
    void installsFromRegistry();
};

void ControllersTest::denoisesVoice()
{
    const QString storeRoot = qEnvironmentVariable("STEMHAND_TEST_MODELS");
    const QString input = qEnvironmentVariable("STEMHAND_TEST_INPUT");
    if (storeRoot.isEmpty() || input.isEmpty())
        QSKIP("set STEMHAND_TEST_MODELS and STEMHAND_TEST_INPUT to run this test");
    if (!QDir(storeRoot).exists() || !QFileInfo::exists(input))
        QSKIP("model fixtures are not present in this checkout");

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

void ControllersTest::mixesStems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const int rate = 44100;
    const qint64 frames = rate; // one second
    const QVector<double> levels{0.1, 0.2, 0.05, 0.0};

    QStringList paths;
    for (int i = 0; i < levels.size(); ++i) {
        const QString path = directory.path() + QStringLiteral("/stem%1.wav").arg(i);
        WavWriter writer;
        QString error;
        QVERIFY2(writer.open(path, 2, rate, frames, &error), qPrintable(error));
        QVector<float> buffer(static_cast<int>(frames) * 2);
        for (int s = 0; s < buffer.size(); ++s)
            buffer[s] = static_cast<float>(levels.at(i));
        QVERIFY2(writer.write(buffer.constData(), frames, &error), qPrintable(error));
        writer.close();
        paths.append(path);
    }

    // QVERIFY/QCOMPARE expand to `return;`, so this helper reports through its
    // return value and the caller does the asserting.
    auto renderAndRead = [&](const QVector<StemState> &states, const QString &name,
                             QVector<float> *out, double *gainOut) -> bool {
        const QString output = directory.path() + QLatin1Char('/') + name;
        QString error;
        double gain = 1.0;
        double seconds = 0.0;
        if (!StemMixer::render(paths, states, output, &error, &gain, &seconds))
            return false;
        if (gainOut)
            *gainOut = gain;
        WavReader reader;
        if (!reader.open(output, &error))
            return false;
        out->resize(static_cast<int>(frames) * 2);
        qint64 got = 0;
        return reader.read(frames, out->data(), &got, &error) && got == frames;
    };

    QVector<StemState> states(4);

    // All audible at unity: the sum, scaled only if it would clip.
    QVector<float> mixed;
    QVERIFY(renderAndRead(states, QStringLiteral("sum.wav"), &mixed, nullptr));
    QCOMPARE(mixed.size(), static_cast<int>(frames) * 2);
    QVERIFY(std::fabs(mixed.at(200) - 0.35f) < 1e-3f);

    // Mute removes one source.
    states[0].muted = true;
    QVERIFY(renderAndRead(states, QStringLiteral("mute.wav"), &mixed, nullptr));
    QVERIFY(std::fabs(mixed.at(200) - 0.25f) < 1e-3f);

    // Solo wins over everything else.
    states[0].muted = false;
    states[1].solo = true;
    QVERIFY(renderAndRead(states, QStringLiteral("solo.wav"), &mixed, nullptr));
    QVERIFY(std::fabs(mixed.at(200) - 0.2f) < 1e-3f);

    // -6 dB halves the amplitude.
    states[1].solo = false;
    states[0].gainDb = -6.0f;
    states[1].muted = true;
    states[2].muted = true;
    QVERIFY(renderAndRead(states, QStringLiteral("gain.wav"), &mixed, nullptr));
    QVERIFY(std::fabs(mixed.at(200) - 0.1f * 0.501187f) < 1e-3f);

    // A hot mix is scaled down instead of clipped.
    states[0].gainDb = 12.0f;
    states[1].gainDb = 12.0f;
    states[2].gainDb = 12.0f;
    states[1].muted = false;
    states[2].muted = false;
    double gain = 1.0;
    QVERIFY(renderAndRead(states, QStringLiteral("hot.wav"), &mixed, &gain));
    QVERIFY(gain < 1.0);
    QVERIFY(std::fabs(mixed.at(200) - 0.99f) < 2e-3f);
}

void ControllersTest::cancelsSeparation()
{
    const QString storeRoot = qEnvironmentVariable("STEMHAND_TEST_MODELS");
    const QString input = qEnvironmentVariable("STEMHAND_TEST_INPUT");
    if (storeRoot.isEmpty() || input.isEmpty())
        QSKIP("set STEMHAND_TEST_MODELS and STEMHAND_TEST_INPUT to run this test");
    if (!QDir(storeRoot).exists() || !QFileInfo::exists(input))
        QSKIP("model fixtures are not present in this checkout");

    ModelStore store;
    store.setRoot(storeRoot);
    store.rescan();

    SeparateController controller(&store);
    if (!controller.hasStemModel())
        QSKIP("no htdemucs model installed");

    QSignalSpy finished(&controller, &SeparateController::finished);
    controller.run(QUrl::fromLocalFile(input));
    controller.cancel();

    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 120000);
    QCOMPARE(finished.count(), 0);
    QVERIFY(controller.stems().isEmpty());
    QVERIFY(controller.message().isEmpty());
}

void ControllersTest::cancelsDenoising()
{
    const QString storeRoot = qEnvironmentVariable("STEMHAND_TEST_MODELS");
    const QString input = qEnvironmentVariable("STEMHAND_TEST_INPUT");
    if (storeRoot.isEmpty() || input.isEmpty())
        QSKIP("set STEMHAND_TEST_MODELS and STEMHAND_TEST_INPUT to run this test");

    ModelStore store;
    store.setRoot(storeRoot);
    store.rescan();

    DenoiseController controller(&store);
    if (!controller.hasVoiceModel())
        QSKIP("no GTCRN model installed");

    QSignalSpy finished(&controller, &DenoiseController::finished);
    controller.run(QUrl::fromLocalFile(input));
    controller.cancel();

    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 60000);
    QCOMPARE(finished.count(), 0);
    QVERIFY(controller.message().isEmpty());
    QVERIFY(controller.error().contains(QStringLiteral("cancelled")));
}

void ControllersTest::playsStems()
{
    const QString folder = qEnvironmentVariable("STEMHAND_TEST_STEMS");
    if (folder.isEmpty())
        QSKIP("set STEMHAND_TEST_STEMS to run this test");

    QStringList paths;
    for (const QString &name : {"drums", "bass", "other", "vocals"}) {
        const QString path = folder + QStringLiteral("/mix3_") + name + QStringLiteral(".wav");
        if (QFileInfo::exists(path))
            paths.append(path);
    }
    if (paths.size() != 4)
        QSKIP("no complete stem set in STEMHAND_TEST_STEMS");

    StemPlayer player;
    QVector<StemState> states(4);
    QVERIFY(player.load(paths, states));
    QVERIFY(player.hasAudio());
    QVERIFY(player.duration() > 1.0);

    player.play();
    QVERIFY(player.playing());
    QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0.2, 8000);

    // Seeking moves the playhead and the readers together.
    player.seek(1.0);
    QVERIFY(std::fabs(player.position() - 1.0) < 0.05);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() > 1.2, 8000);

    player.stop();
    QVERIFY(!player.playing());
    QCOMPARE(player.position(), 0.0);
}

void ControllersTest::remembersMixState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString stems = directory.path() + QStringLiteral("/song_stems");
    QVERIFY(QDir().mkpath(stems));

    const int rate = 44100;
    const qint64 frames = rate / 2;
    for (const QString &name : {"drums", "bass", "other", "vocals"}) {
        WavWriter writer;
        QString error;
        QVERIFY(writer.open(stems + QStringLiteral("/song_") + name + QStringLiteral(".wav"), 2,
                            rate, frames, &error));
        QVector<float> buffer(static_cast<int>(frames) * 2, 0.1f);
        QVERIFY(writer.write(buffer.constData(), frames, &error));
        writer.close();
    }

    ModelStore store;
    store.setRoot(directory.path() + QStringLiteral("/models"));

    const QUrl source = QUrl::fromLocalFile(directory.path() + QStringLiteral("/song.wav"));

    {
        SeparateController controller(&store);
        controller.discover(source);
        QCOMPARE(controller.stems().size(), 4);
        controller.setGain(1, -6.0);
        controller.toggleMute(0);
        controller.toggleSolo(3);
        QTest::qWait(500); // let the debounced save land
    }

    QVERIFY(QFileInfo::exists(stems + QStringLiteral("/mix.json")));

    SeparateController reopened(&store);
    reopened.discover(source);
    const QVariantList restored = reopened.stems();
    QCOMPARE(restored.size(), 4);

    auto byName = [&](const QString &name) {
        for (const QVariant &item : restored) {
            const QVariantMap map = item.toMap();
            if (map.value(QStringLiteral("name")).toString() == name)
                return map;
        }
        return QVariantMap();
    };

    QCOMPARE(byName(QStringLiteral("drums")).value(QStringLiteral("muted")).toBool(), true);
    QCOMPARE(byName(QStringLiteral("bass")).value(QStringLiteral("gain")).toDouble(), -6.0);
    QCOMPARE(byName(QStringLiteral("vocals")).value(QStringLiteral("solo")).toBool(), true);
    QCOMPARE(byName(QStringLiteral("other")).value(QStringLiteral("muted")).toBool(), false);
}

void ControllersTest::installsFromRegistry()
{
    const QString registry = qEnvironmentVariable("STEMHAND_TEST_REGISTRY");
    if (registry.isEmpty())
        QSKIP("set STEMHAND_TEST_REGISTRY to run this test");
    if (!QFileInfo::exists(registry))
        QSKIP("registry fixture is not present in this checkout");

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
