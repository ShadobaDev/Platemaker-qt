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
    // Runs the whole render synchronously on this thread (this call blocks until every
    // slice is processed, or m_cancel is triggered). The pipeline reports back through
    // three plain-C++ callbacks; each one just re-emits the data as a Qt signal so the
    // GUI thread (connected via a queued connection) can react without touching pipeline
    // internals directly. m_outcome captures the final success/failure/cancellation result.
    m_outcome = pipeline.run(
        m_inputs, m_outProfile, m_canvasProfiles, m_canvasProfileIds, m_outputDir,
        m_cancel,
        // Per-slice progress tick: how many of the total slices are done, and which one just finished.
        [this](const ProcessingProgress& p) {
            emit progress(p.sliceDone, p.sliceTotal, QString::fromStdString(p.sliceName));
        },
        // Pipeline log line (info/warning/error) — forwarded verbatim for the GUI's log view.
        [this](ProcessingLogLevel level, const std::string& msg) {
            emit log(static_cast<int>(level), QString::fromStdString(msg));
        },
        // Fired the moment a slice file is actually written to disk, so the output tile
        // list can append it live instead of waiting for the whole render to finish.
        [this](const std::string& name, const std::string& fullPath) {
            emit sliceSaved(QString::fromStdString(name), QString::fromStdString(fullPath));
        },
        // Restricts the run to m_onlySlices when a partial re-render was requested
        // (see setOnlySlices()); nullptr means "render everything".
        m_onlySlices.empty() ? nullptr : &m_onlySlices);

    emit finished();
}
