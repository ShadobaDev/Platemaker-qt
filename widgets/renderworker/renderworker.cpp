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

    // The pipeline reports back through plain-C++ callbacks, called synchronously on THIS worker
    // thread. Each lambda does one cheap thing — re-emit the data as a Qt signal — so the GUI
    // thread (connected via a queued connection) reacts without touching pipeline internals, and
    // the render is never blocked on the UI. Only the callbacks the GUI uses are wired; the rest
    // stay null. m_outcome captures the final success/failure/cancellation result.
    ProcessingCallbacks callbacks;
    // Per-slice progress tick: how many of the total slices are done, and which one just finished.
    callbacks.onProgress = [this](const ProcessingProgress& p) {
        emit progress(p.sliceDone, p.sliceTotal, QString::fromStdString(p.sliceName));
    };
    // Pipeline log line (info/warning/error) — forwarded verbatim for the GUI's log view.
    callbacks.onLog = [this](ProcessingLogLevel level, const std::string& msg) {
        emit log(static_cast<int>(level), QString::fromStdString(msg));
    };
    // Fired the moment a slice file is written to disk, carrying its 0-based row index so the
    // output tile at that position can be replaced live (same for partial and full renders).
    callbacks.onSliceSaved = [this](const SliceSaved& s) {
        emit sliceSaved(s.sliceIndex,
                        QString::fromStdString(s.name),
                        QString::fromStdString(s.fullPath));
    };
    // Fired once per input during phase 1 (before any slice exists) — appended to the strip, or
    // skipped with a reason. Carries the path + Core::InputStatus so the matching input tile can
    // update live (green when used, violet "Skipped" when no profile matched, etc.).
    callbacks.onInput = [this](const InputResult& r) {
        emit inputStatus(QString::fromStdString(r.inputPath),
                         static_cast<int>(r.status));
    };

    // The worker's members are exactly a RenderRequest — both exist so a render never touches the
    // live workspace — so assembling one here is a copy, not a translation.
    RenderRequest request;
    request.inputs            = m_inputs;
    request.outputProfile     = m_outProfile;
    request.canvasProfiles    = m_canvasProfiles;
    request.canvasProfileIds  = m_canvasProfileIds;
    request.outputDirectory   = m_outputDir;
    request.thumbnailCacheDir = m_thumbnailCacheDir;
    request.colourCorrection  = m_colourCorrection;
    request.stripOverlays     = m_stripOverlays;
    // Absent = full render; a partial re-render restricts it to the dirty slice names.
    if (!m_onlySlices.empty())
        request.onlySlices = m_onlySlices;

    // Runs the whole render synchronously on this thread (blocks until every slice is processed
    // or m_cancel is triggered).
    m_outcome = ProcessingPipeline::render(request, m_cancel, callbacks);

    emit finished();
}
