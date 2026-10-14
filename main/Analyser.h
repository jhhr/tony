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
#include <optional>
#include <set>
#include <vector>

#include "AnalysisSettings.h"
#include "TakeEvents.h"
#include "VoiceTracks.h"
#include "VoiceGate.h"
#include "VoiceThreshold.h"

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

    // Add to values, in Hz, those of the pitch track and the notes that
    // are drawn between the frames start and end, of whichever of the
    // two are on show: the pitch a zoom of the frequency range keeps in
    // view (TouchGestures)
    void getPitchOnShow(sv::sv_frame_t start, sv::sv_frame_t end,
                        std::vector<double> &values) const;

    // Return completion %age for initial analysis -- 100 means it's done
    int getInitialAnalysisCompletion();

    enum Component {
        Audio = 0,
        PitchTrack = 1,
        Notes = 2,
        Spectrogram = 3,
    };

    // For the user's own toggles: each writes its own setting (in this
    // analyser's group, see getSettingsGroup()), which every file this
    // analyser shows later starts from.  Not for a temporary state: for
    // that, the layer's showLayer() or its play parameters
    bool isVisible(Component c) const;
    void setVisible(Component c, bool v);
    void toggleVisible(Component c) { setVisible(c, !isVisible(c)); }

    bool isAudible(Component c) const;
    void setAudible(Component c, bool v);
    void toggleAudible(Component c) { setAudible(c, !isAudible(c)); }

    /**
     * Keep a track hidden, or silent, whatever the settings say: for
     * what the command line leaves out (--no-spectrogram,
     * --no-sonification), which is not the user's choice.  Applied now
     * and again whenever this analyser loads its layers (Analyse Now
     * makes them anew), and written to no setting, so that a launch
     * with the track has it as the user left it.
     */
    void keepHidden(Component c);
    void keepSilent(Component c);

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

    // For the user's own level controls, as setVisible() and
    // setAudible() are for the toggles: each writes its own setting.
    // Only the reference's audio, pitch and notes have controls and
    // keep a level: the singing analyser keeps none
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
     * The user's own options, which a new song is analysed with: each
     * setting of getAnalysisSettings(), or its default where the user
     * has never chosen one.
     */
    static AnalysisSettings::Values getUserAnalysisSettings();

    /**
     * Which voice the reference's pitch track follows where two or
     * three sing, as the pyin fork's voicefollow parameter numbers
     * them: the option "voice-to-follow", read as one of the two the
     * menu offers, the strongest and the lower.  The fork can also
     * follow the higher one, which a selection's pitch candidates offer,
     * and the middle one, which Tony does not offer: neither holds
     * through a whole song well enough to choose for one
     * (docs/open-points.md).
     */
    enum VoiceToFollow {
        FollowStrongest = 0,
        FollowLower = 1,
        FollowHigher = 2,
        FollowMiddle = 3
    };
    static int voiceToFollow(const AnalysisSettings::Values &options);

    /**
     * The options this analyser's analyses are made with: those of the
     * song open in the window, which owns them and keeps them for as
     * long as this analyser lives.  Without them, the user's own, from
     * the settings (an analyser of a test, on its own).
     */
    void setAnalysisOptions(const AnalysisSettings::Values *options) {
        m_analysisOptions = options;
    }
    AnalysisSettings::Values getAnalysisOptions() const;

    /**
     * The options the pitch track and notes of our audio were made with:
     * set when an analysis of the whole file makes them, and by the
     * window from the session that brings them back.  None where that is
     * not known -- a session saved before the options were kept -- or
     * nothing has been analysed.  Edits made to the track since do not
     * change it.
     */
    std::optional<AnalysisSettings::Values> getAnalysedWith() const {
        return m_analysedWith;
    }
    void setAnalysedWith(std::optional<AnalysisSettings::Values> options) {
        m_analysedWith = options;
    }

    /**
     * The reference's voice tracks (VoiceTracks.h): its pitch track as
     * the whole-file analysis gave it following each voice, made in the
     * same run, or kept by the session that brought the track back.
     * They are there once that run has finished; until then, and for the
     * singing's analyser always, a selection has no candidates of its
     * own and there is no voice to prefer.
     */
    bool haveVoiceTracks() const { return m_voiceTracks.isComplete(); }
    const VoiceTracks &getVoiceTracks() const { return m_voiceTracks; }

    /**
     * The voice tracks a session kept, for the pitch track and notes
     * this analyser has just claimed from it.
     */
    void setVoiceTracks(const VoiceTracks &tracks);

    /**
     * Make the voice tracks of a pitch track that was analysed before
     * they were kept, as a session saved then brings it back: a run of
     * the whole-file analysis that gives the voices' tracks alone, and
     * leaves the pitch track and notes as they are. Nothing if there are
     * tracks, a run is on its way, or there is no pitch track. "" or a
     * user-readable error string.
     */
    QString analyseVoiceTracks();

    /// A run that makes the voice tracks is on its way
    bool isMakingVoiceTracks() const { return !m_voiceLayers.empty(); }

    /**
     * Put a voice's track into the pitch track within the selection, in
     * place of what it held there: Prefer Lower Voice and Prefer Higher
     * Voice. Nothing without the voice tracks. Makes the edit commands
     * of the pitch track's layer: the caller wraps them in a compound
     * command.
     */
    void chooseVoice(sv::Selection sel, VoiceTracks::Voice voice);

    /**
     * Make the pitch candidates for a selection, and keep what the pitch
     * track holds there for abandonReAnalysis(). Those of the reference
     * are its voice tracks within the selection, made at once (or as
     * soon as the voice tracks are, if they are still being made); those
     * of a frequency range (isConstrained()) are analysed, and come
     * asynchronously. Returns "" on success or a user-readable error
     * string on failure.
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
     * With a voice threshold (VoiceThreshold) the run's pitch and notes
     * are gated by it before they are merged (VoiceGate): what was found
     * in audio quieter than that is not merged, and the notes are
     * trimmed to what was not.  The threshold is the run's from here on,
     * whatever the setting becomes; Off merges the run as it is.
     *
     * Returns "" if a run was started (or there was nothing to do), or
     * a user-readable error string.  A second call while one is running
     * abandons the first: its material is presumed to have changed.
     */
    QString analyseRange(sv::sv_frame_t start, sv::sv_frame_t end,
                         sv::sv_frame_t clipStart = 0,
                         sv::sv_frame_t clipEnd = -1,
                         double voiceThreshold = VoiceThreshold::kOff);

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
     * For the tests only: while held, a ranged analysis that has
     * finished is not merged and counts as running.  A test that has to
     * act during the analysis of a short range cannot count on it
     * otherwise: pYIN may finish before analyseRange() returns, and the
     * merge is then over with.  Letting go merges a finished result at
     * once, as its completion would have; one still running is merged
     * when it finishes.  Never held in the application.
     */
    void setRangedMergeHeld(bool held);

    /**
     * For the tests only: analyse with pYIN's own parameters, as the
     * singing's analyser does, though this is the reference's.  A test
     * that checks where a take was placed by comparing its pitch steps
     * with the reference's needs the two analysed alike: at a leap the
     * reference's analysis glides to the new note and reaches it two or
     * three hops before pYIN's own (docs/open-points.md).  Never set in
     * the application.
     */
    void setPlainAnalysis(bool plain) { m_plainAnalysis = plain; }

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
     * of the main pitch track to the candidate a step up or down from
     * what it holds now (pitchCandidateStep()). Nothing changes if
     * there is none.
     */
    void switchPitchCandidate(sv::Selection sel, bool up);

    /**
     * The candidate a step up (or down) from what the pitch track holds
     * in the selection: the lowest of the candidates above it (the
     * highest of those below), as PitchCandidates::step() compares
     * them. -1 if there is none, or no candidates yet.
     */
    int pitchCandidateStep(sv::Selection sel, bool up) const;

    /**
     * Put candidate index into the pitch track within the selection, in
     * place of what it held there, as far as the selection is the one
     * the candidates are of: they hold nothing elsewhere. Makes the edit
     * commands of the pitch track's layer: the caller wraps them in a
     * compound command.
     */
    void chooseCandidate(sv::Selection sel, int index);

    /**
     * True once the re-analysis of this selection has finished, with
     * candidates or without; pitchCandidatesReady() is emitted then.
     */
    bool havePitchCandidatesFor(sv::Selection sel) const;

    /**
     * A re-analysis of a frequency range is on its way: the layers of
     * its transform have not come back yet.
     */
    bool isReAnalysing() const {
        return m_currentAsyncHandle != 0;
    }

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

    // The re-analysis of the selection has finished: its pitch
    // candidates, if it found any, are there to choose from
    void pitchCandidatesReady();

    // The voice tracks have been made, or set from a session, or have
    // gone with the analysis they were of (haveVoiceTracks())
    void voiceTracksChanged();

protected slots:
    void layerAboutToBeDeleted(sv::Layer *);
    void layerCompletionChanged(sv::ModelId);
    void reAnalyseRegion(sv::sv_frame_t, sv::sv_frame_t, float, float);
    void materialiseReAnalysis();
    void rangedAnalysisCompletionChanged(sv::ModelId);
    void candidateCompletionChanged(sv::ModelId);
    void voiceTracksCompletionChanged(sv::ModelId);

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
    bool m_reAnalysisDone;
    bool m_candidatesVisible;
    sv::Document::LayerCreationAsyncHandle m_currentAsyncHandle;
    QMutex m_asyncMutex;

    // See haveVoiceTracks()
    VoiceTracks m_voiceTracks;

    // The voices' outputs of a run that is making the voice tracks, by
    // VoiceTracks::Voice: registered with the document, so that deleting
    // them releases their models, and in no view, as a ranged
    // analysis's are. Empty when no run is on its way
    std::vector<sv::Layer *> m_voiceLayers;

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

    // The voice threshold of the run, with the blocks it was built with:
    // the merge gates the run's result with it
    VoiceGate m_rangedGate;

    // Set only by a test (setRangedMergeHeld())
    bool m_rangedMergeHeld;

    // Set only by a test (setPlainAnalysis())
    bool m_plainAnalysis;

    // See setAnalysisOptions() and getAnalysedWith()
    const AnalysisSettings::Values *m_analysisOptions;
    std::optional<AnalysisSettings::Values> m_analysedWith;

    // What the last merge did, for the undo command of the recording
    // that asked for the analysis (see getRangedPitchChange())
    TakeEvents::Change m_rangedPitchChange;
    TakeEvents::Change m_rangedNotesChange;

    // See setWaveformFaded()
    bool m_waveformFaded;

    // See keepHidden() and keepSilent()
    std::set<Component> m_keptHidden;
    std::set<Component> m_keptSilent;

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
    // whole-file analysis produces. With voiceTracks, for the reference,
    // the three voices' track outputs follow, from the same run, where
    // the plugin has them (see VoiceTracks). "" on success, else an error
    // string
    QString buildAnalysisTransforms(sv::Transforms &transforms,
                                    bool voiceTracks = false);

    // Take the layers of the voices' outputs of a run just started, by
    // VoiceTracks::Voice, and listen for its end
    void startVoiceRun(const std::vector<sv::Layer *> &layers);

    // Stop a run making the voice tracks, if one is going, and delete
    // its layers
    void discardVoiceRun();

    // Forget the voice tracks, saying so if there were any
    void clearVoiceTracks();

    // Merge a finished ranged analysis into the pitch and notes models
    // and delete the temporaries
    void mergeRangedAnalysis();

    // The run's pitch events and notes (in frames of the file) less what
    // m_rangedGate finds under its threshold in our audio
    void gateRangedEvents(sv::EventVector &pitch, sv::EventVector &notes) const;

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

    // Whether a selection's pitch candidates are the voices, cut from
    // the voice tracks: the reference's. Otherwise they are pYIN's own,
    // LocalCandidatePYIN's
    bool offersVoices() const;

    // The candidates of the selection being re-analysed, from the voice
    // tracks, if they are there: each voice's track within it, those
    // that are the same line there given once (see the definition)
    void makeVoiceCandidates();

    // A pitch candidate's layer into the pane, as one: see the definition
    void adoptCandidate(sv::TimeValueLayer *layer);

    // Document::LayerCreationHandler method
    void layersCreated(sv::Document::LayerCreationAsyncHandle,
                       std::vector<sv::Layer *>, std::vector<sv::Layer *>);

    // Where this analyser keeps its toggles: PlaybackSettings'
    // reference group, or the singing one for the secondary colours
    QString getSettingsGroup() const;

    // Apply the settings to our layers, writing none
    void loadState(Component c);

    // Whether a track keeps its level and pan in the settings: see
    // setGain()
    bool keepsLevel(Component c) const;

    // setVisible(), setAudible(), setGain() and setPan() without the
    // write to the settings.  False if there is no layer (or play
    // parameters) to apply it to
    bool applyVisible(Component c, bool v);
    bool applyAudible(Component c, bool a);
    bool applyGain(Component c, float gain);
    bool applyPan(Component c, float pan);
};

#endif
