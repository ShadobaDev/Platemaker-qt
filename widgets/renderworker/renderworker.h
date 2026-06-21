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
 * \brief Runs Core::ProcessingPipeline on a worker thread, marshalling progress,
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
    RenderWorker(std::vector<Platemaker::Models::InputFile>     inputs,
                 Platemaker::Models::OutputProfile              outProfile,
                 std::vector<Platemaker::Models::CanvasProfile> canvasProfiles,
                 std::vector<std::string>                       canvasProfileIds,
                 std::string                                    outputDir,
                 const Platemaker::Infrastructure::CancellationToken& cancel,
                 QObject* parent = nullptr);

    [[nodiscard]] const Platemaker::Core::ProcessingOutcome& outcome() const { return m_outcome; }

    /**
     * \brief Restricts the run to a partial re-render of the named output slices.
     *
     * When non-empty, only slices whose output file name is in \p names are
     * encoded, saved and recorded; all others are skipped. Empty (default) →
     * full render. Call before starting the worker.
     */
    void setOnlySlices(std::unordered_set<std::string> names) { m_onlySlices = std::move(names); }

    /// True when a partial-render filter has been set (some slices restricted).
    [[nodiscard]] bool isPartial() const { return !m_onlySlices.empty(); }

public slots:
    void process();

signals:
    void progress(int done, int total, QString sliceName);
    void log(int level, QString message);
    void sliceSaved(QString name, QString fullPath);
    void finished();

private:
    std::vector<Platemaker::Models::InputFile>     m_inputs;
    Platemaker::Models::OutputProfile              m_outProfile;
    std::vector<Platemaker::Models::CanvasProfile> m_canvasProfiles;
    std::vector<std::string>                       m_canvasProfileIds;
    std::string                                    m_outputDir;
    const Platemaker::Infrastructure::CancellationToken& m_cancel;
    std::unordered_set<std::string>                m_onlySlices; //!< Partial-render filter (empty = full).
    Platemaker::Core::ProcessingOutcome            m_outcome;
};

#endif // RENDERWORKER_H
