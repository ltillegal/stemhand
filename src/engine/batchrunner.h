#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace stemhand {

class GtcrnDenoiser;
class HtdemucsSeparator;

enum class BatchOperation {
    Denoise,   // GTCRN, 16 kHz mono
    Separate   // htdemucs, 44.1 kHz stereo, four stems per file
};

struct BatchResult {
    QString input;
    QString output; // a WAV file for denoise, a folder of stems for separate
    bool ok = false;
    QString error;
    double audioSeconds = 0.0;
    double wallSeconds = 0.0;
};

// Runs the same operation over many files, loading the model once.
//
// A failing file does not stop the batch: the whole point of a queue is to come
// back to a folder of results and one error message, not to lose the other
// twenty files because the third had an unsupported codec. Cancellation is
// honouring the same rule at file granularity — the file in flight is finished
// or abandoned, the rest are marked as not attempted.
class BatchRunner
{
public:
    explicit BatchRunner(BatchOperation operation);
    ~BatchRunner();

    BatchRunner(const BatchRunner &) = delete;
    BatchRunner &operator=(const BatchRunner &) = delete;

    static QString operationName(BatchOperation operation);
    static bool operationFromName(const QString &name, BatchOperation *operation);

    bool load(const QString &modelPath, int threads, QString *error);

    bool run(const QStringList &inputs, const QString &outputDirectory,
             QVector<BatchResult> *results,
             const std::function<void(int, int)> &fileStarted = {},
             const std::function<void(int, int)> &fileProgress = {},
             const std::atomic_bool *cancel = nullptr);

private:
    BatchOperation m_operation;
    std::unique_ptr<GtcrnDenoiser> m_denoiser;
    std::unique_ptr<HtdemucsSeparator> m_separator;
};

} // namespace stemhand
