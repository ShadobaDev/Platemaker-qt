#include "renderworker.h"

RenderWorker::RenderWorker(std::vector<Platemaker::Models::InputFile>     inputs,
                           Platemaker::Models::OutputProfile              outProfile,
                           std::vector<Platemaker::Models::CanvasProfile> canvasProfiles,
                           std::vector<std::string>                       canvasProfileIds,
                           std::string                                    outputDir,
                           const Platemaker::Infrastructure::CancellationToken& cancel,
                           QObject* parent)
    : QObject(parent)
    , m_inputs(std::move(inputs))
    , m_outProfile(std::move(outProfile))
    , m_canvasProfiles(std::move(canvasProfiles))
    , m_canvasProfileIds(std::move(canvasProfileIds))
    , m_outputDir(std::move(outputDir))
    , m_cancel(cancel)
{
}

void RenderWorker::process()
{
    using namespace Platemaker::Core;

    ProcessingPipeline pipeline;
    m_outcome = pipeline.run(
        m_inputs, m_outProfile, m_canvasProfiles, m_canvasProfileIds, m_outputDir,
        m_cancel,
        [this](const ProcessingProgress& p) {
            emit progress(p.sliceDone, p.sliceTotal, QString::fromStdString(p.sliceName));
        },
        [this](ProcessingLogLevel level, const std::string& msg) {
            emit log(static_cast<int>(level), QString::fromStdString(msg));
        },
        [this](const std::string& name, const std::string& fullPath) {
            emit sliceSaved(QString::fromStdString(name), QString::fromStdString(fullPath));
        },
        m_onlySlices.empty() ? nullptr : &m_onlySlices);

    emit finished();
}
