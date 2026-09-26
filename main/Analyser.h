/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef ANALYSER_H
#define ANALYSER_H

#include <QObject>
#include <QRect>
#include <QMutex>

#include <map>
#include <vector>

#include "TakeEvents.h"

#include "framework/Document.h"
#include "base/Selection.h"
#include "base/Clipboard.h"
#include "data/model/WaveFileModel.h"

namespace sv {
class Pane;
class PaneStack;
class Layer;
class TimeValueLayer;
class Layer;
}

class Analyser : public QObject,
                 public sv::Document::LayerCreationHandler
{
    Q_OBJECT

public:
    /**
     * Color scheme for the pitch track and notes layers.
     * Primary is used for the reference/target track (black pitch, blue notes).
     * Secondary is used for the singer/recording track (orange pitch, purple notes).
     */
    enum ColorScheme {
        PrimaryColors,   // Black pitch track, Bright Blue notes
        SecondaryColors, // Orange pitch track, Bright Purple notes
    };

    Analyser(ColorScheme colorScheme = PrimaryColors);
    virtual ~Analyser();

    // Process new main model, add derived layers; return "" on
    // success or error string on failure.
    // If deferAnalysis is true, skip running pYIN (waveform and
    // visualisation layers are still created).  Use this when the
    // model is a WritableWaveFileModel that is still being recorded
    // into; call analyseExistingFile() once recording completes.
    QString newFileLoaded(sv::Document *newDocument,
                          sv::ModelId model,
                          sv::PaneStack *paneStack,
                          sv::Pane *pane,
                          bool deferAnalysis = false);

    // Remove any derived layers, process the main model, add derived
    // layers; return "" on success or error string on failure
    QString analyseExistingFile();

    // Stop any pitch or note analysis still running on our model, and
    // wait for its thread to exit. A running transform holds shared
    // pointers to its input and output models, so this must happen
    // before those models are released, or the last reference may be
    // dropped (and the model destroyed) on the transform thread
    void cancelAnalyses();

    // Discard any layers etc associated with the current document
    void fileClosed();

    // Remove all layers this analyser owns from the pane and delete them
    // from the document, then call fileClosed().  Use this instead of
    // fileClosed() when the analyser is being torn down while the document
    // is still alive (e.g. when replacing a singing-track recording).
    // Unlike fileClosed(), this actually cleans up the view and the document
    // model registry so no orphan layers or models remain.
    void removeAllLayers();

    // Give up our layers without deleting the pitch and notes ones: they
    // stay in the pane, with their events, for the analyser of another
    // audio file to claim (see MainWindow::swapSingingAudio()).  Only the
    // waveform layer goes, which releases the audio model it shows, and
    // then fileClosed() as above.  This is for the singing analyser; the
    // primary's spectrogram would be left in the pane as well.
    void releaseLayers();

    void setIntelligentActions(bool);

    bool getDisplayFrequencyExtents(double &min, double &max);
    bool setDisplayFrequencyExtents(double min, double max);

    // Return completion %age for initial analysis -- 100 means it's done
    int getInitialAnalysisCompletion();

    enum Component {
        Audio = 0,
        PitchTrack = 1,
        Notes = 2,
        Spectrogram = 3,
    };

    bool isVisible(Component c) const;
    void setVisible(Component c, bool v);
    void toggleVisible(Component c) { setVisible(c, !isVisible(c)); }

    bool isAudible(Component c) const;
    void setAudible(Component c, bool v);
    void toggleAudible(Component c) { setAudible(c, !isAudible(c)); }

    /**
     * Draw the waveform paler than usual, for while something is drawn
     * over it (the lyrics), or back in its usual grey.  Remembered, so
     * that a waveform this analyser makes or takes over later is drawn
     * the same way.  Unlike setVisible() and setAudible() this is not a
     * setting: nothing is written to QSettings, and nothing is marked
     * modified.  A session saves the colour with the layer, so whoever
     * calls this has to call it again after a load.
     */
    void setWaveformFaded(bool faded);
    bool isWaveformFaded() const { return m_waveformFaded; }

    void cycleStatus(Component c) {
        if (isVisible(c)) {
            if (isAudible(c)) {
                setVisible(c, false);
                setAudible(c, false);
            } else {
                setAudible(c, true);
            }
        } else {
            setVisible(c, true);
            setAudible(c, false);
        }
    }

    sv::ModelId getMainModelId() const {
        return m_fileModel;
    }
    std::shared_ptr<sv::WaveFileModel> getMainModel() const {
        return sv::ModelById::getAs<sv::WaveFileModel>(m_fileModel);
    }

    float getGain(Component c) const;
    void setGain(Component c, float gain);

    float getPan(Component c) const;
    void setPan(Component c, float pan);

    void getEnclosingSelectionScope(sv::sv_frame_t f, sv::sv_frame_t &f0, sv::sv_frame_t &f1);

    struct FrequencyRange {
        FrequencyRange() : min(0), max(0) { }
        FrequencyRange(double min_, double max_) : min(min_), max(max_) { }
        bool isConstrained() const { return min != max; }
        double min;
        double max;
        bool operator==(const FrequencyRange &r) {
            return min == r.min && max == r.max;
        }
    };

    /**
     * Return the QSettings keys, and their default values, that
     * affect analysis behaviour. These all live within the Analyser
     * group in QSettings.
     */
    static std::map<QString, QVariant> getAnalysisSettings();
    
    /**
     * Analyse the selection and schedule asynchronous adds of
     * candidate layers for the region it contains. Returns "" on
     * success or a user-readable error string on failure. If the
     * frequency range isConstrained(), analysis will be constrained
     * to that range.
     */
    QString reAnalyseSelection(sv::Selection sel, FrequencyRange range);

    /**
     * Analyse the frames from start to end of our audio file with the
     * same transforms and parameters as a whole-file analysis, and
     * merge the result into the pitch track and notes we already have
     * (which must both be there: this analyser has either made them or
     * claimed them).  The range is widened by half a second on each
     * side, so that a note at an edge is found whole, but never outside
     * clipStart..clipEnd -- the caller passes the extent of the
     * material that is there to be analysed (the take's coverage; the
     * Analyser knows nothing of Coverage itself).  A clipEnd below zero
     * means the end of the file.
     *
     * The analysis runs into temporary layers that are in no view;
     * when both are complete their events replace what was in the
     * middle of the run -- a quarter of a second each side of the range
     * asked for, or out to an end of the run that was clipped -- and
     * rangedAnalysisMerged() and initialAnalysisCompleted() are emitted.
     * The rest of the run is context only: it is where pYIN knows least,
     * so what is there already is left alone.  The merge makes no command
     * of its own; what it changed is kept, for the command of the
     * recording that asked for it (getRangedPitchChange()).
     *
     * Returns "" if a run was started (or there was nothing to do), or
     * a user-readable error string.  A second call while one is running
     * abandons the first: its material is presumed to have changed.
     */
    QString analyseRange(sv::sv_frame_t start, sv::sv_frame_t end,
                         sv::sv_frame_t clipStart = 0,
                         sv::sv_frame_t clipEnd = -1);

    /**
     * Make an empty pitch track and empty notes for our audio, where a
     * whole-file analysis would have made them full: the first
     * recording of a take has nothing to keep, but analyseRange() needs
     * models to merge its result into (spec 6.2, last paragraph).  The
     * layers, their models and everything set on them are as an
     * analysed pair's are, so that this analyser's own scan for
     * existing layers, a swap and a session restore cannot tell the two
     * apart.  Does nothing if both layers are there already; an odd one
     * out is replaced.  "" on success, else an error string.
     */
    QString addEmptyAnalyses();

    /**
     * Return true between the start of a ranged analysis and the merge
     * (or the abandonment) of its result.
     */
    bool isAnalysingRange() const {
        return !m_rangedLayers.empty();
    }

    /**
     * Abandon a ranged analysis if one is running, merging nothing.  For
     * an undo of the recording that asked for it: the result must not
     * land on a take that has been put back as it was.
     */
    void cancelRangedAnalysis() { discardRangedAnalysis(); }

    /**
     * For the tests: while held, a ranged analysis that has finished is
     * left unmerged, and isAnalysingRange() stays true, just as while
     * pYIN is still running.  Releasing merges a run that finished
     * while held.  A test that does something during the analysis of a
     * take needs the run still there when it does it, and pYIN over
     * less than a second of audio can finish, on a fast machine, before
     * analyseRange() has even returned.  Off unless a test sets it.
     */
    void setHoldRangedMerge(bool hold);

    /**
     * What the last ranged merge took out of and put into the pitch
     * track and the notes.  Reversing these two changes undoes the
     * merge, which is how the recording that asked for it is made
     * undoable (the merge itself still puts nothing on the undo stack).
     * Valid from rangedAnalysisMerged() until the next analyseRange().
     */
    const TakeEvents::Change &getRangedPitchChange() const {
        return m_rangedPitchChange;
    }
    const TakeEvents::Change &getRangedNotesChange() const {
        return m_rangedNotesChange;
    }

    /**
     * Return true if the analysed pitch candidates are currently
     * visible (they are hidden from the call to reAnalyseSelection
     * until they are requested through showPitchCandidates()). Note
     * that this may return true even when no pitch candidate layers
     * actually exist yet, because they are constructed
     * asynchronously. If that is the case, then the layers will
     * appear when they are created (otherwise they will remain hidden
     * after creation).
     */
    bool arePitchCandidatesShown() const;

    /**
     * Show or hide the analysed pitch candidate layers. This is reset
     * (to "hide") with each new call to reAnalyseSelection. Because
     * the layers are created asynchronously, setting this to true
     * does not guarantee that they appear immediately, only that they
     * will appear once they have been created.
     */
    void showPitchCandidates(bool shown);

    /**
     * If a re-analysis has been activated, switch the selected area
     * of the main pitch track to a different candidate from the
     * analysis results.
     */
    void switchPitchCandidate(sv::Selection sel, bool up);

    /**
     * Return true if it is possible to switch up to another pitch
     * candidate. This may mean that the currently selected pitch
     * candidate is not the highest, or it may mean that no alternate
     * pitch candidate has been selected at all yet (but some are
     * available).
     */
    bool haveHigherPitchCandidate() const;

    /**
     * Return true if it is possible to switch down to another pitch
     * candidate. This may mean that the currently selected pitch
     * candidate is not the lowest, or it may mean that no alternate
     * pitch candidate has been selected at all yet (but some are
     * available).
     */
    bool haveLowerPitchCandidate() const;

    /**
     * Delete the pitch estimates from the selected area of the main
     * pitch track.
     */
    void deletePitches(sv::Selection sel);

    /**
     * Move the main pitch track and any active analysis candidate
     * tracks up or down an octave in the selected area.
     */
    void shiftOctave(sv::Selection sel, bool up);

    /**
     * Remove any re-analysis layers and also reset the pitch track in
     * the given selection to its state prior to the last re-analysis,
     * abandoning any changes made since then. No re-analysis layers
     * will be available until after the next call to
     * reAnalyseSelection.
     */
    void abandonReAnalysis(sv::Selection sel);

    /**
     * Remove any re-analysis layers, without any expectation of
     * adding them later, unlike showPitchCandidates(false), and
     * without changing the current pitch track, unlike
     * abandonReAnalysis().
     */
    void clearReAnalysis();

    /**
     * Import the pitch track from the given layer into our
     * pitch-track layer.
     */
    void takePitchTrackFrom(sv::Layer *layer);

    ColorScheme getColorScheme() const {
        return m_colorScheme;
    }

    sv::Pane *getPane() {
        return m_pane;
    }

    sv::Layer *getLayer(Component type) {
        return m_layers[type];
    }

    // Raise the pitch track, then the notes, to the top of the pane,
    // where the editing tools find them.  For when something else has
    // been added to the pane on top of them
    void stackLayers();

signals:
    void layersChanged();
    void initialAnalysisCompleted();

    // A ranged analysis has just been merged into the pitch and notes,
    // and getRangedPitchChange() / getRangedNotesChange() say what it
    // changed.  Emitted before initialAnalysisCompleted()
    void rangedAnalysisMerged();

protected slots:
    void layerAboutToBeDeleted(sv::Layer *);
    void layerCompletionChanged(sv::ModelId);
    void reAnalyseRegion(sv::sv_frame_t, sv::sv_frame_t, float, float);
    void materialiseReAnalysis();
    void rangedAnalysisCompletionChanged(sv::ModelId);

protected:
    ColorScheme m_colorScheme;

    sv::Document *m_document;
    sv::ModelId m_fileModel;
    sv::PaneStack *m_paneStack;
    sv::Pane *m_pane;

    mutable std::map<Component, sv::Layer *> m_layers;

    sv::Clipboard m_preAnalysis;
    sv::Selection m_reAnalysingSelection;
    FrequencyRange m_reAnalysingRange;
    std::vector<sv::Layer *> m_reAnalysisCandidates;
    int m_currentCandidate;
    bool m_candidatesVisible;
    sv::Document::LayerCreationAsyncHandle m_currentAsyncHandle;
    QMutex m_asyncMutex;

    // A ranged analysis in progress (analyseRange()). The layers are
    // registered with the document but added to no view, so nothing
    // shows them, nothing selects them and claimExistingAnalyses(),
    // which looks in the pane, never takes them for ours
    std::vector<sv::Layer *> m_rangedLayers;
    sv::ModelId m_rangedPitchModel;
    sv::ModelId m_rangedNotesModel;
    sv::sv_frame_t m_rangedStart;  // widened, grid-aligned: what is analysed
    sv::sv_frame_t m_rangedEnd;
    sv::sv_frame_t m_rangedMergeStart; // the window inside that which the
    sv::sv_frame_t m_rangedMergeEnd;   // merge replaces (W)
    // True where the run's far edge is the edge of the caller's coverage:
    // there is no context to keep there, so the merge takes in what the
    // run stamped past it.  The near edge needs no such flag -- a run
    // cannot stamp anything before its own first two hops anyway
    bool m_rangedClippedEnd;

    // See setHoldRangedMerge()
    bool m_holdRangedMerge;

    // What the last merge did, for the undo command of the recording
    // that asked for the analysis (see getRangedPitchChange())
    TakeEvents::Change m_rangedPitchChange;
    TakeEvents::Change m_rangedNotesChange;

    // See setWaveformFaded()
    bool m_waveformFaded;

    QString doAllAnalyses(bool withPitchTrack);

    QString addVisualisations();
    QString addWaveform();
    QString addAnalyses();

    // The colour the waveform is to be drawn in now: see
    // setWaveformFaded()
    int getWaveformColour() const;

    // The colours and play parameters of the pitch and notes layers,
    // whichever way they were made
    void configureAnalysisLayers();

    // The singing track's pitch and notes are to be seen and not heard:
    // there is a pitch track and a set of notes being sonified already
    void silenceSecondaryAnalysisLayers();

    // The two pYIN transforms of a full analysis (smoothed pitch track
    // and notes) with the parameters the settings ask for. Shared with
    // analyseRange(), which must produce within its range just what a
    // whole-file analysis produces. "" on success, else an error string
    QString buildAnalysisTransforms(sv::Transforms &transforms);

    // Merge a finished ranged analysis into the pitch and notes models
    // and delete the temporaries
    void mergeRangedAnalysis();

    // Stop a ranged analysis if one is running and delete its
    // temporary layers and models, merging nothing
    void discardRangedAnalysis();

    // Claim the pitch and notes layers that are in the pane already and
    // whose models come from our file model: the layers of a session just
    // restored, or the ones another audio file has been swapped under.
    // True only if both are there.  An odd one out is removed from the
    // pane when removeMismatched is set (the caller is about to make the
    // pair itself) and left alone otherwise.
    bool claimExistingAnalyses(bool removeMismatched);

    // Listen to the pitch and notes layers we have just claimed or made
    void connectAnalysisLayers();

    void discardPitchCandidates();
    
    // Document::LayerCreationHandler method
    void layersCreated(sv::Document::LayerCreationAsyncHandle,
                       std::vector<sv::Layer *>, std::vector<sv::Layer *>);

    void saveState(Component c) const;
    void loadState(Component c);
};

#endif
