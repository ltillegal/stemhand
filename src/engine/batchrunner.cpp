#include "batchrunner.h"

#include "audioio.h"
#include "gtcrn.h"
#include "htdemucs.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <vector>

namespace stemhand {
namespace {

QString denoisedName(const QString &input)
{
    const QFileInfo info(input);
    return info.completeBaseName() + QStringLiteral(" (stemhand denoised).wav");
}

QString stemsFolderName(const QString &input)
{
    return QFileInfo(input).completeBaseName() + QStringLiteral("_stems");
}

} // namespace

BatchRunner::BatchRunner(BatchOperation operation)
    : m_operation(operation)
{
}

BatchRunner::~BatchRunner() = default;

QString BatchRunner::operationName(BatchOperation operation)
{
    return operation == BatchOperation::Denoise ? QStringLiteral("denoise")
                                                : QStringLiteral("separate");
}

bool BatchRunner::operationFromName(const QString &name, BatchOperation *operation)
{
    if (name == QLatin1String("denoise")) {
        *operation = BatchOperation::Denoise;
        return true;
    }
    if (name == QLatin1String("separate")) {
        *operation = BatchOperation::Separate;
        return true;
    }
    return false;
}

bool BatchRunner::load(const QString &modelPath, int threads, QString *error)
{
    if (m_operation == BatchOperation::Denoise) {
        m_denoiser = std::make_unique<GtcrnDenoiser>();
        return m_denoiser->load(modelPath, threads, error);
    }

    m_separator = std::make_unique<HtdemucsSeparator>();
    return m_separator->load(modelPath, threads, error);
}

bool BatchRunner::run(const QStringList &inputs, const QString &outputDirectory,
                      QVector<BatchResult> *results,
                      const std::function<void(int, int)> &fileStarted,
                      const std::function<void(int, int)> &fileProgress,
                      const std::atomic_bool *cancel)
{
    if (!QDir().mkpath(outputDirectory)) {
        if (results) {
            BatchResult failure;
            failure.error = QStringLiteral("cannot create %1").arg(outputDirectory);
            results->append(failure);
        }
        return false;
    }

    const int total = static_cast<int>(inputs.size());
    for (int index = 0; index < total; ++index) {
        if (cancel && cancel->load()) {
            if (results) {
                for (int rest = index; rest < total; ++rest) {
                    BatchResult skipped;
                    skipped.input = inputs.at(rest);
                    skipped.error = QStringLiteral("cancelled before processing");
                    results->append(skipped);
                }
            }
            return false;
        }

        if (fileStarted)
            fileStarted(index, total);

        const QString input = inputs.at(index);
        BatchResult result;
        result.input = input;

        QElapsedTimer timer;
        timer.start();

        if (m_operation == BatchOperation::Denoise) {
            QVector<float> decoded;
            if (!decodeToMono(input, m_denoiser->sampleRate(), &decoded, &result.error)) {
                results->append(result);
                continue;
            }

            std::vector<float> samples(decoded.begin(), decoded.end());
            result.audioSeconds = samples.size() / static_cast<double>(m_denoiser->sampleRate());

            if (!m_denoiser->process(samples, &result.error, fileProgress, cancel)) {
                results->append(result);
                continue;
            }

            QVector<float> output(samples.begin(), samples.end());
            preventClipping(&output);

            result.output = outputDirectory + QLatin1Char('/') + denoisedName(input);
            if (!writeWav(result.output, output, m_denoiser->sampleRate(), &result.error)) {
                results->append(result);
                continue;
            }
        } else {
            QVector<float> decoded;
            if (!decodeToChannels(input, HtdemucsSeparator::kSampleRate,
                                  HtdemucsSeparator::kChannels, &decoded, &result.error)) {
                results->append(result);
                continue;
            }

            result.audioSeconds = decoded.size()
                / (HtdemucsSeparator::kChannels
                   * static_cast<double>(HtdemucsSeparator::kSampleRate));

            result.output = outputDirectory + QLatin1Char('/') + stemsFolderName(input);
            if (!m_separator->separate(decoded, result.output,
                                       QFileInfo(input).completeBaseName(), &result.error,
                                       fileProgress, cancel)) {
                results->append(result);
                continue;
            }
        }

        result.wallSeconds = timer.elapsed() / 1000.0;
        result.ok = true;
        results->append(result);
    }

    return true;
}

} // namespace stemhand
