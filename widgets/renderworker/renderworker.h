#ifndef RENDERWORKER_H
#define RENDERWORKER_H

#include <QObject>

#include <string>
#include <unordered_set>
#include <vector>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

/**
 * @brief Runs Core::ProcessingPipeline on a worker thread, marshalling progress,
 *        logs and per-slice notifications to the GUI via queued signals.
 *
 * The worker is constructed with COPIES of everything it needs (input files,
 * profiles, output dir) so it never touches the live workspace. It holds a const
 * reference to a CancellationToken owned by MainWindow (which outlives the run).
 * The outcome is stored and read by the main thread in the `finished` slot before
 * the worker is deleted.
 */
class RenderWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a worker holding copies of everything needed to run a render,
     * so it never touches the live workspace.
     * @param inputs The input files to process.
     * @param outProfile The output profile (format/size/quality) to render with.
     * @param canvasProfiles The workspace's canvas profiles, copied for lookup by ID.
     * @param canvasProfileIds The IDs of the canvas profiles assigned to the project being rendered.
     * @param outputDir The directory slices are written into.
     * @param cancel A cancellation token owned by MainWindow, checked during the run; must outlive the worker.
     * @param parent The parent object, if any.
     */
    RenderWorker(std::vector<Platemaker::Models::InputFile>     inputs,
                 Platemaker::Models::OutputProfile              outProfile,
                 std::vector<Platemaker::Models::CanvasProfile> canvasProfiles,
                 std::vector<std::string>                       canvasProfileIds,
                 std::string                                    outputDir,
                 const Platemaker::Infrastructure::CancellationToken& cancel,
                 QObject* parent = nullptr);

    [[nodiscard]] const Platemaker::Core::ProcessingOutcome& outcome() const { return m_outcome; } //!< Result of the run, populated by process(); valid once `finished` has been emitted.

    /**
     * \brief Restricts the run to a partial re-render of the named output slices.
     *
     * When non-empty, only slices whose output file name is in \p names are
     * encoded, saved and recorded; all others are skipped. Empty (default) →
     * full render. Call before starting the worker.
     */
    void setOnlySlices(std::unordered_set<std::string> names) { m_onlySlices = std::move(names); }

    [[nodiscard]] bool isPartial() const { return !m_onlySlices.empty(); } //!< True when a partial-render filter has been set (some slices restricted).

public slots:
    void process();   //!< Runs Core::ProcessingPipeline synchronously (intended to execute on a worker thread), emitting progress/log/sliceSaved as it goes, then `finished`.

signals:
    void progress(int done, int total, QString sliceName);  //!< Emitted as each slice completes; \p done / \p total are slice counts, \p sliceName the slice just finished.
    void log(int level, QString message);                   //!< Emitted for pipeline log messages; \p level is a ProcessingLogLevel value.
    void sliceSaved(QString name, QString fullPath);        //!< Emitted right after a slice file is written to disk, for live output-tile updates.
    void finished();                                        //!< Emitted when the run completes (success, failure, or cancellation); outcome() is valid at this point.

private:
    std::vector<Platemaker::Models::InputFile>     m_inputs;            //!< Input files to render (copy, taken at construction).
    Platemaker::Models::OutputProfile              m_outProfile;        //!< Output format/size/quality to render with (copy).
    std::vector<Platemaker::Models::CanvasProfile> m_canvasProfiles;    //!< Workspace canvas profiles, for lookup by ID (copy).
    std::vector<std::string>                       m_canvasProfileIds;  //!< IDs of the canvas profiles assigned to the project being rendered.
    std::string                                    m_outputDir;         //!< Directory slices are written into.
    const Platemaker::Infrastructure::CancellationToken& m_cancel;      //!< Cancellation token owned by MainWindow; checked during the run.
    std::unordered_set<std::string>                m_onlySlices;        //!< Partial-render filter (empty = full).
    Platemaker::Core::ProcessingOutcome            m_outcome;           //!< Result of the run, populated by process().
};

#endif // RENDERWORKER_H
