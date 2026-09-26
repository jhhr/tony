/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */
/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef TEST_SINGING_ANALYSIS_H
#define TEST_SINGING_ANALYSIS_H

// Tier 4: the Analyser running the real pYIN plugin on synthetic
// audio, in primary and secondary mode, and the latency shift that
// the record path applies before analysis. No MainWindow.
//
// The test main points VAMP_PATH at the directory holding the test
// executable, which is where the build leaves pyin.dll.

#include "TestSignals.h"

#include "../Analyser.h"

#include "framework/Document.h"
#include "framework/SVFileReader.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/Layer.h"
#include "layer/ColourDatabase.h"
#include "layer/SingleColourLayer.h"
#include "layer/WaveformLayer.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "transform/ModelTransformerFactory.h"
#include "base/PlayParameters.h"

#include <QObject>
#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

class TestSingingAnalysis : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;
    static constexpr int hop = 256; // as set in Analyser::addAnalyses()

    // The latency shift used throughout: close to half a second, and
    // a whole number of hops so that the shifted and unshifted
    // analyses land on the same frame grid
    static constexpr sv::sv_frame_t shift = 86 * hop;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    sv::ViewManager *m_viewManager = nullptr;
    sv::PaneStack *m_paneStack = nullptr;
    sv::Document *m_document = nullptr;
    sv::Pane *m_pane = nullptr;

    sv::ModelId makeAudioModel(const std::vector<float> &data) {
        QString path = m_dir.filePath
            (QString("audio-%1.wav").arg(++m_fileCounter));
        auto model = std::make_shared<sv::WritableWaveFileModel>
            (path, rate, 1,
             sv::WritableWaveFileModel::Normalisation::None);
        const float *ptr = data.data();
        model->addSamples(&ptr, sv::sv_frame_t(data.size()));
        model->writeComplete();
        return sv::ModelById::add(model);
    }

    // Both tones have a whole number of samples per period (200 and
    // 150). A synthetic tone is exactly periodic, so when its period is
    // not a whole number of samples the best integer lag is a multiple
    // of it, and pYIN reports a subharmonic: 220, 330 and 440 Hz all
    // come out as 110 Hz (lag 401), where 150 Hz (lag 294) is right.
    // No voice is that periodic, so this is about the test signal and
    // not about the application.
    static constexpr double referenceHz = 220.5;
    static constexpr double singingHz = 294.0;

    static std::vector<float> tone(double hz, double seconds) {
        return TestSignals::sawtooth(hz, rate, int(seconds * rate), 0.5f);
    }

    static std::vector<float> silenceThenTone(sv::sv_frame_t silence,
                                              double hz, double seconds) {
        std::vector<float> data(size_t(silence), 0.f);
        auto t = tone(hz, seconds);
        data.insert(data.end(), t.begin(), t.end());
        return data;
    }

    static void appendSilence(std::vector<float> &data, double seconds) {
        data.insert(data.end(), size_t(seconds * rate), 0.f);
    }

    // Four notes with 0.6 s of silence between them, 5 s in all: the
    // gaps are wide enough that a range with half a second of margin on
    // each side can sit over the third note and still leave the second
    // and fourth untouched. Each pitch has a whole number of samples per
    // period, as above (150, 200, 180 and 140 samples)
    static std::vector<float> fourNotes() {
        std::vector<float> data;
        for (double hz : { 294.0, 220.5, 245.0, 315.0 }) {
            appendSilence(data, 0.6);
            auto t = tone(hz, 0.5);
            data.insert(data.end(), t.begin(), t.end());
        }
        appendSilence(data, 0.6);
        return data;
    }

    // The third note of fourNotes() is at 2.8 to 3.3 s; this range holds
    // it whole and is nowhere near frame 0. Widened by half a second it
    // runs from 2.25 to 3.85 s, so that both of its edges are in silence
    // and the notes on either side are left out of it altogether
    static constexpr double thirdNoteFrom = 2.75;
    static constexpr double thirdNoteTo = 3.35;

    // The range analyseRange() really covers: half a second either side
    // of the range asked for, clipped to the coverage, then out to the
    // 256-frame grid
    static void widenRange(sv::sv_frame_t start, sv::sv_frame_t end,
                           sv::sv_frame_t clipStart, sv::sv_frame_t clipEnd,
                           sv::sv_frame_t &from, sv::sv_frame_t &to) {
        sv::sv_frame_t margin = sv::sv_frame_t(rate / 2);
        from = std::max(clipStart, start - margin);
        to = std::min(clipEnd, end + margin);
        from = (from / hop) * hop;
        to = ((to + hop - 1) / hop) * hop;
    }

    static sv::sv_frame_t frameAt(double seconds) {
        return sv::sv_frame_t(seconds * rate);
    }

    // Run the analyser on the model and wait for pYIN. If startFrame
    // is non-zero, apply it between layer setup and analysis, which
    // is what MainWindow::analyseNow() does after a take.
    void analyse(Analyser &analyser, sv::ModelId model,
                 sv::sv_frame_t startFrame = 0) {
        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QString error;
        if (startFrame != 0) {
            error = analyser.newFileLoaded
                (m_document, model, m_paneStack, m_pane, true);
            QCOMPARE(error, QString());
            auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>(model);
            QVERIFY(wave);
            wave->setStartFrame(startFrame);
            error = analyser.analyseExistingFile();
        } else {
            error = analyser.newFileLoaded
                (m_document, model, m_paneStack, m_pane, false);
        }
        QCOMPARE(error, QString());
        QVERIFY(analyser.getLayer(Analyser::PitchTrack));
        QVERIFY(analyser.getLayer(Analyser::Notes));
        QVERIFY2(done.count() > 0 || done.wait(30000),
                 "pYIN did not complete within 30 seconds");

        // and for the transform to be gone. The first notice at 100 comes
        // from one of the two outputs, and the other's can still be
        // queued behind it: a test would count that as a second
        // initialAnalysisCompleted(), long after pYIN finished. The
        // factory lets a transform go only after its notices are in
        QTRY_VERIFY_WITH_TIMEOUT(!sv::ModelTransformerFactory::getInstance()
                                 ->haveRunningTransformers(), 30000);
    }

    sv::ModelId addSingingModel(const std::vector<float> &data) {
        sv::ModelId id = makeAudioModel(data);
        m_document->addNonDerivedModel(id);
        return id;
    }

    static sv::EventVector pitchEvents(Analyser &analyser) {
        sv::Layer *layer = analyser.getLayer(Analyser::PitchTrack);
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (layer->getModel());
        if (!model) return {};
        return model->getAllEvents();
    }

    static std::shared_ptr<sv::NoteModel> notesModel(Analyser &analyser) {
        sv::Layer *layer = analyser.getLayer(Analyser::Notes);
        if (!layer) return nullptr;
        return sv::ModelById::getAs<sv::NoteModel>(layer->getModel());
    }

    static sv::EventVector noteEvents(Analyser &analyser) {
        auto model = notesModel(analyser);
        if (!model) return {};
        return model->getAllEvents();
    }

    // The singing track as MainWindow sets it up for the first recording
    // of a take: the analyser takes the audio without analysing any of it
    // and makes empty pitch and notes layers for the analysis of the
    // recorded range to be merged into
    void setUpEmpty(Analyser &analyser, sv::ModelId singing) {
        QCOMPARE(analyser.newFileLoaded(m_document, singing, m_paneStack,
                                        m_pane, true), QString());
        QCOMPARE(analyser.addEmptyAnalyses(), QString());
        QVERIFY(analyser.getLayer(Analyser::PitchTrack));
        QVERIFY(analyser.getLayer(Analyser::Notes));
        QVERIFY(pitchEvents(analyser).empty());
        QVERIFY(noteEvents(analyser).empty());
    }

    // Wait for a ranged analysis to be merged. The completion signal
    // alone does not say so: the whole-file analysis before it can still
    // be delivering its own, late, when the range starts
    void waitForRange(Analyser &analyser, QSignalSpy &done) {
        QVERIFY2(done.count() > 0 || done.wait(30000),
                 "the ranged analysis did not complete within 30 seconds");
        QTRY_VERIFY2_WITH_TIMEOUT(!analyser.isAnalysingRange(),
                                  "the analyser still says a range is being "
                                  "analysed", 30000);
    }

    // Nothing of a ranged analysis may be left behind in the document
    void verifyNothingLeftOver(size_t layersBefore, size_t modelsBefore) {
        QCOMPARE(m_document->getLayers().size(), layersBefore);
        QCOMPARE(m_document->getModels().size(), modelsBefore);
    }

    // Pitch events whose frame is within [from, to), by frame
    static std::map<sv::sv_frame_t, float> pitchIn(const sv::EventVector &events,
                                                   sv::sv_frame_t from,
                                                   sv::sv_frame_t to) {
        std::map<sv::sv_frame_t, float> m;
        for (const auto &e : events) {
            if (e.getFrame() >= from && e.getFrame() < to) {
                m[e.getFrame()] = e.getValue();
            }
        }
        return m;
    }

    // Notes that start within [from, to)
    static sv::EventVector notesIn(const sv::EventVector &events,
                                   sv::sv_frame_t from, sv::sv_frame_t to) {
        sv::EventVector out;
        for (const auto &e : events) {
            if (e.getFrame() >= from && e.getFrame() < to) out.push_back(e);
        }
        return out;
    }

    // The window analyseRange() merges: half the margin on each side of
    // the range asked for, inside the run, out to an edge of the run that
    // the coverage limit clipped
    static void mergeWindow(sv::sv_frame_t start, sv::sv_frame_t end,
                            sv::sv_frame_t clipStart, sv::sv_frame_t clipEnd,
                            sv::sv_frame_t &wFrom, sv::sv_frame_t &wTo) {
        sv::sv_frame_t margin = sv::sv_frame_t(rate / 2);
        sv::sv_frame_t from, to;
        widenRange(start, end, clipStart, clipEnd, from, to);
        wFrom = (start - margin < clipStart) ?
            from : std::max(from, start - margin / 2);
        wTo = (end + margin > clipEnd) ? to : std::min(to, end + margin / 2);
    }

    static std::set<sv::sv_frame_t> doubledFrames(const sv::EventVector &ev) {
        std::set<sv::sv_frame_t> out;
        for (size_t i = 1; i < ev.size(); ++i) {
            if (ev[i].getFrame() == ev[i-1].getFrame()) {
                out.insert(ev[i].getFrame());
            }
        }
        return out;
    }

    static std::map<sv::sv_frame_t, float> byFrame(const sv::EventVector &ev) {
        std::map<sv::sv_frame_t, float> m;
        for (const auto &e : ev) m[e.getFrame()] = e.getValue();
        return m;
    }

    // For a failure message: every note as onset+duration@Hz
    static QString describeNotes(const sv::EventVector &events) {
        QStringList out;
        for (const auto &e : events) {
            out << QString("%1+%2@%3").arg(e.getFrame())
                .arg(e.getDuration()).arg(double(e.getValue()), 0, 'f', 1);
        }
        return "[" + out.join(" ") + "]";
    }

    // The pitch track of the whole file before and after a ranged run
    // over audio that has not changed: the same frames, no frame twice,
    // the same values outside the merge window and near enough inside it
    void comparePitchAcrossFile(const sv::EventVector &was,
                                const sv::EventVector &is,
                                sv::sv_frame_t wFrom, sv::sv_frame_t wTo) {
        // The merge may not leave two events on one frame. pYIN itself
        // does, once per run: in fixed-lag mode it stamps the frame 100
        // hops before the end of a run twice, so a frame that was already
        // doubled in the whole-file track may still be
        QStringList twice;
        for (sv::sv_frame_t f : doubledFrames(is)) {
            if (!doubledFrames(was).count(f)) {
                twice << QString::number(f);
            }
        }
        QVERIFY2(twice.isEmpty(),
                 qPrintable(QString("two pitch events on one frame after the "
                                    "merge (window %1 to %2): %3")
                            .arg(wFrom).arg(wTo).arg(twice.join(" "))));

        auto a = byFrame(was), b = byFrame(is);
        QStringList odd;
        for (const auto &p : a) if (!b.count(p.first)) odd << QString("-%1").arg(p.first);
        for (const auto &p : b) if (!a.count(p.first)) odd << QString("+%1").arg(p.first);
        QVERIFY2(odd.isEmpty(),
                 qPrintable(QString("pitch frames lost (-) or gained (+) by "
                                    "the ranged run, merge window %1 to %2: %3")
                            .arg(wFrom).arg(wTo).arg(odd.join(" "))));
        double worst = 0.0;
        for (const auto &p : a) {
            float now = b[p.first];
            if (p.first >= wFrom && p.first < wTo) {
                worst = std::max(worst, std::abs
                                 (TestSignals::centsBetween(now, p.second)));
            } else {
                QVERIFY2(now == p.second,
                         qPrintable(QString("pitch at %1 changed from %2 to "
                                            "%3, outside the merge window "
                                            "%4 to %5").arg(p.first)
                                    .arg(p.second).arg(now)
                                    .arg(wFrom).arg(wTo)));
            }
        }
        // Measured: 0 cents. The window keeps a quarter of a second of
        // the run away from its edges, and that is context enough for
        // pYIN to say the same as it said about the whole file
        QVERIFY2(worst < 20.0,
                 qPrintable(QString("worst pitch difference inside the merge "
                                    "window %1 cents").arg(worst)));
    }

    static double medianHz(const sv::EventVector &events) {
        std::vector<double> values;
        for (const auto &e : events) values.push_back(e.getValue());
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    static int colourOf(sv::Layer *layer) {
        auto scl = qobject_cast<sv::SingleColourLayer *>(layer);
        return scl ? scl->getBaseColour() : -1;
    }

    static bool audible(sv::Layer *layer) {
        auto params = layer ? layer->getPlayParameters() : nullptr;
        return params && params->isPlayAudible();
    }

    struct Callback : public sv::SVFileReaderPaneCallback {
        sv::PaneStack *stack;
        Callback(sv::PaneStack *s) : stack(s) { }
        sv::Pane *addPane() override { return stack->addPane(); }
        void setWindowSize(int, int) override { }
        void addSelection(sv::sv_frame_t, sv::sv_frame_t) override { }
    };

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());

        // These are the test executable's own settings (see the
        // organisation name in the test main). Start from the
        // defaults whatever an earlier run left behind.
        QSettings().clear();

        // As the MainWindow and MainWindowBase constructors do. The
        // Analyser connects to signals by their unqualified argument
        // type names, which only resolve once these are registered.
        qRegisterMetaType<sv::sv_frame_t>("sv_frame_t");
        qRegisterMetaType<sv::sv_samplerate_t>("sv_samplerate_t");
        qRegisterMetaType<sv::ModelId>("ModelId");
        QSettings settings;
        settings.beginGroup("Transformer");
        settings.setValue("use-flexi-note-model", true);
        settings.endGroup();

        // As the MainWindow constructor does; only the colours that
        // the Analyser asks for by name
        auto cdb = sv::ColourDatabase::getInstance();
        cdb->addColour(Qt::black, "Black");
        cdb->addColour(QColor(255, 150, 50), "Orange");
        cdb->addColour(QColor(180, 180, 180), "Grey");
        cdb->setUseDarkBackground
            (cdb->addColour(QColor(30, 150, 255), "Bright Blue"), true);
        cdb->setUseDarkBackground
            (cdb->addColour(QColor(225, 74, 255), "Bright Purple"), true);
    }

    void init() {
        m_viewManager = new sv::ViewManager;
        m_paneStack = new sv::PaneStack(nullptr, m_viewManager);
        m_document = new sv::Document;
        m_document->setMainModel(makeAudioModel(tone(referenceHz, 2.0)));
        m_pane = m_paneStack->addPane();
    }

    void cleanup() {
        delete m_document;
        delete m_paneStack;
        delete m_viewManager;
        m_document = nullptr;
        m_paneStack = nullptr;
        m_viewManager = nullptr;
        m_pane = nullptr;
    }

    void pitch_track_found() {
        Analyser analyser;
        analyse(analyser, m_document->getMainModel());
        if (QTest::currentTestFailed()) return;

        auto events = pitchEvents(analyser);
        QVERIFY2(events.size() > 100,
                 qPrintable(QString("only %1 pitch events for 2 s of tone")
                            .arg(events.size())));

        double median = medianHz(events);
        double cents = TestSignals::centsBetween(median, referenceHz);
        QVERIFY2(std::abs(cents) < 10.0,
                 qPrintable(QString("median %1 Hz is %2 cents from the reference")
                            .arg(median).arg(cents)));
    }

    void shift_aligns_onset() {
        // The core latency-compensation test. The same recording,
        // half a second of silence and then a tone, is analysed with
        // and without the start-frame shift that analyseNow() applies.
        auto data = silenceThenTone(shift, singingHz, 1.5);

        sv::sv_frame_t unshifted = 0, shifted = 0;

        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            auto events = pitchEvents(analyser);
            QVERIFY(!events.empty());
            unshifted = events.begin()->getFrame();
            analyser.removeAllLayers();
        }

        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data), -shift);
            if (QTest::currentTestFailed()) return;
            auto events = pitchEvents(analyser);
            QVERIFY(!events.empty());
            shifted = events.begin()->getFrame();
            analyser.removeAllLayers();
        }

        // Measured: 22016 and 0, the onset exactly. Two hops either
        // way allows for pYIN deciding a little later that the tone
        // is voiced; the difference between the runs gets one.
        QVERIFY2(std::abs(unshifted - shift) <= 2 * hop,
                 qPrintable(QString("unshifted onset at %1, expected near %2")
                            .arg(unshifted).arg(shift)));
        QVERIFY2(std::abs(shifted) <= 2 * hop,
                 qPrintable(QString("shifted onset at %1, expected near 0")
                            .arg(shifted)));
        QVERIFY2(std::abs((unshifted - shifted) - shift) <= hop,
                 qPrintable(QString("onset moved by %1, expected %2")
                            .arg(unshifted - shifted).arg(shift)));
    }

    void shift_survives_session() {
        sv::ModelId singing = addSingingModel(tone(singingHz, 0.5));
        auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>(singing);
        QVERIFY(wave);
        wave->setStartFrame(-shift);
        QString singingPath = wave->getLocation();

        // The model is only written if a layer in a view uses it
        sv::Layer *layer = m_document->createLayer(sv::LayerFactory::Waveform);
        QVERIFY(layer);
        m_document->setModel(layer, singing);
        m_document->addLayerToView(m_pane, layer);

        // As MainWindowBase::toXml()
        QString xml;
        {
            QTextStream out(&xml);
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                << "<!DOCTYPE sonic-visualiser>\n<sv>\n";
            m_document->toXml(out, "", "");
            out << "<display>\n";
            m_pane->toXml(out, "  ");
            out << "</display>\n</sv>\n";
        }
        QVERIFY2(xml.contains(QString("start=\"%1\"").arg(-shift)),
                 "the shifted start frame was not written to the session");

        sv::ViewManager viewManager;
        sv::PaneStack paneStack(nullptr, &viewManager);
        auto document = new sv::Document;
        Callback callback(&paneStack);
        {
            sv::SVFileReader reader(document, callback, m_dir.path());
            reader.parseXml(xml);
            QVERIFY2(reader.isOK(), qPrintable(reader.getErrorString()));
        }

        std::shared_ptr<sv::WaveFileModel> reloaded;
        for (sv::Layer *l : document->getLayers()) {
            auto m = sv::ModelById::getAs<sv::WaveFileModel>(l->getModel());
            if (m && m->getLocation() == singingPath) reloaded = m;
        }
        sv::sv_frame_t start = reloaded ? reloaded->getStartFrame() : 0;
        bool found = bool(reloaded);
        reloaded.reset();
        delete document;

        QVERIFY2(found, "the singing model was not reloaded");
        QCOMPARE(start, -shift);
    }

    void secondary_layers_muted() {
        // Guards commit 36d0ce7
        Analyser primary(Analyser::PrimaryColors);
        analyse(primary, m_document->getMainModel());
        if (QTest::currentTestFailed()) return;

        Analyser secondary(Analyser::SecondaryColors);
        analyse(secondary, addSingingModel(tone(singingHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        QVERIFY(primary.getLayer(Analyser::PitchTrack) !=
                secondary.getLayer(Analyser::PitchTrack));
        QVERIFY(primary.getLayer(Analyser::Notes) !=
                secondary.getLayer(Analyser::Notes));

        QVERIFY(!audible(secondary.getLayer(Analyser::PitchTrack)));
        QVERIFY(!audible(secondary.getLayer(Analyser::Notes)));
        QVERIFY(audible(primary.getLayer(Analyser::PitchTrack)));
        QVERIFY(audible(primary.getLayer(Analyser::Notes)));

        // Muting them must not have gone through the settings that
        // the two analysers share
        QSettings settings;
        settings.beginGroup("Analyser");
        QVERIFY(settings.value
                (QString("audible-%1").arg(int(Analyser::PitchTrack)),
                 true).toBool());
        QVERIFY(settings.value
                (QString("audible-%1").arg(int(Analyser::Notes)),
                 true).toBool());
        settings.endGroup();
    }

    void secondary_colours() {
        Analyser primary(Analyser::PrimaryColors);
        analyse(primary, m_document->getMainModel());
        if (QTest::currentTestFailed()) return;

        Analyser secondary(Analyser::SecondaryColors);
        analyse(secondary, addSingingModel(tone(singingHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        auto cdb = sv::ColourDatabase::getInstance();
        QCOMPARE(colourOf(primary.getLayer(Analyser::PitchTrack)),
                 cdb->getColourIndex(QString("Black")));
        QCOMPARE(colourOf(primary.getLayer(Analyser::Notes)),
                 cdb->getColourIndex(QString("Bright Blue")));
        QCOMPARE(colourOf(secondary.getLayer(Analyser::PitchTrack)),
                 cdb->getColourIndex(QString("Orange")));
        QCOMPARE(colourOf(secondary.getLayer(Analyser::Notes)),
                 cdb->getColourIndex(QString("Bright Purple")));
    }

    void secondary_tracks_own_audio() {
        // Two analysers in one pane: each pitch track must come from
        // its own model, which the two different pitches show
        Analyser primary(Analyser::PrimaryColors);
        analyse(primary, m_document->getMainModel());
        if (QTest::currentTestFailed()) return;

        Analyser secondary(Analyser::SecondaryColors);
        analyse(secondary, addSingingModel(tone(singingHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        double p = medianHz(pitchEvents(primary));
        double s = medianHz(pitchEvents(secondary));
        QVERIFY2(std::abs(TestSignals::centsBetween(p, referenceHz)) < 10.0,
                 qPrintable(QString("primary median %1 Hz").arg(p)));
        QVERIFY2(std::abs(TestSignals::centsBetween(s, singingHz)) < 10.0,
                 qPrintable(QString("secondary median %1 Hz").arg(s)));
    }

    void secondary_waveform_is_own_model() {
        // The secondary analyser must show the singing audio, not
        // make a second layer on the document's main model
        sv::ModelId singing = addSingingModel(tone(singingHz, 1.0));
        Analyser secondary(Analyser::SecondaryColors);
        QString error = secondary.newFileLoaded
            (m_document, singing, m_paneStack, m_pane, true);
        QCOMPARE(error, QString());

        sv::Layer *audio = secondary.getLayer(Analyser::Audio);
        QVERIFY(audio);
        QVERIFY(audio->getModel() == singing);

        // deferAnalysis: no pYIN yet
        QVERIFY(!secondary.getLayer(Analyser::PitchTrack));
        QVERIFY(!secondary.getLayer(Analyser::Notes));
    }

    void remove_all_layers() {
        Analyser primary(Analyser::PrimaryColors);
        analyse(primary, m_document->getMainModel());
        if (QTest::currentTestFailed()) return;

        sv::ModelId singing = addSingingModel(tone(singingHz, 1.0));
        Analyser secondary(Analyser::SecondaryColors);
        analyse(secondary, singing);
        if (QTest::currentTestFailed()) return;

        sv::Layer *primaryPitch = primary.getLayer(Analyser::PitchTrack);
        int primaryLayers = 0;
        for (sv::Layer *l : m_document->getLayers()) {
            if (l->getModel() != singing && l->getSourceModel() != singing) {
                ++primaryLayers;
            }
        }

        secondary.removeAllLayers();

        for (sv::Layer *l : m_document->getLayers()) {
            QVERIFY2(l->getModel() != singing,
                     "a layer still shows the singing model");
            QVERIFY2(l->getSourceModel() != singing,
                     "a layer derived from the singing model is left");
        }
        QVERIFY2(!sv::ModelById::get(singing),
                 "the singing model was not released");
        QVERIFY(!secondary.getLayer(Analyser::Audio));
        QVERIFY(!secondary.getLayer(Analyser::PitchTrack));
        QVERIFY(!secondary.getLayer(Analyser::Notes));

        // and the primary analyser's layers were left alone
        QCOMPARE(int(m_document->getLayers().size()), primaryLayers);
        QVERIFY(primary.getLayer(Analyser::PitchTrack) == primaryPitch);
        QVERIFY(m_document->getLayers().count(primaryPitch) > 0);
    }

    void ranged_matches_whole_file() {
        // The time-stamp question of spec section 11: within its range a
        // ranged analysis must give what a whole-file analysis gives.
        // Also the "nothing in the models yet" case: the ranged result
        // is all there is afterwards
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t start = frameAt(thirdNoteFrom);
        sv::sv_frame_t end = frameAt(thirdNoteTo);
        sv::sv_frame_t from, to;
        widenRange(start, end, 0, fileEnd, from, to);

        sv::EventVector wholePitch, wholeNotes;
        {
            Analyser whole(Analyser::SecondaryColors);
            analyse(whole, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            wholePitch = pitchEvents(whole);
            wholeNotes = noteEvents(whole);
            whole.removeAllLayers();
        }
        QCOMPARE(int(wholeNotes.size()), 4);

        Analyser analyser(Analyser::SecondaryColors);
        setUpEmpty(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        size_t layers = m_document->getLayers().size();
        size_t models = m_document->getModels().size();

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(done.count(), 1);
        verifyNothingLeftOver(layers, models);

        // Nothing may have landed outside the range that was analysed:
        // this is what fails if the time stamps of an output are relative
        // to the start of the run rather than to the start of the file
        for (const auto &e : pitchEvents(analyser)) {
            QVERIFY2(e.getFrame() >= from && e.getFrame() < to + 2 * hop,
                     qPrintable(QString("pitch event at %1, outside the "
                                        "analysed %2 to %3")
                                .arg(e.getFrame()).arg(from).arg(to)));
        }
        for (const auto &e : noteEvents(analyser)) {
            QVERIFY2(e.getFrame() >= from && e.getFrame() < to + 2 * hop,
                     qPrintable(QString("note at %1, outside the analysed "
                                        "%2 to %3")
                                .arg(e.getFrame()).arg(from).arg(to)));
        }

        // Both runs place their events on the grid of the step size: the
        // pitch track because its output is at a fixed sample rate of one
        // per step, the notes because the range analysed starts on that
        // grid (take that away and the notes land between the grid points)
        for (const auto &e : wholePitch) QCOMPARE(e.getFrame() % hop, 0);
        for (const auto &e : wholeNotes) QCOMPARE(e.getFrame() % hop, 0);
        for (const auto &e : pitchEvents(analyser)) {
            QCOMPARE(e.getFrame() % hop, 0);
        }
        for (const auto &e : noteEvents(analyser)) {
            QCOMPARE(e.getFrame() % hop, 0);
        }

        // The pitch track within the range asked for, frame by frame
        auto a = pitchIn(wholePitch, start, end);
        auto b = pitchIn(pitchEvents(analyser), start, end);
        QVERIFY2(b.size() > 60,
                 qPrintable(QString("only %1 ranged pitch events in the range")
                            .arg(b.size())));

        int odd = 0;
        double worst = 0.0;
        for (const auto &p : a) {
            auto i = b.find(p.first);
            if (i == b.end()) { ++odd; continue; }
            double cents = std::abs(TestSignals::centsBetween(i->second,
                                                              p.second));
            if (cents > worst) worst = cents;
        }
        for (const auto &p : b) {
            if (a.find(p.first) == a.end()) ++odd;
        }
        // Measured: 0 frames in one run and not the other, 0 cents apart.
        // A few frames of slack at the voiced edges, where pYIN's HMM has
        // less to go on in the shorter run
        QVERIFY2(odd <= 6,
                 qPrintable(QString("%1 of %2 frames are in one run and not "
                                    "the other").arg(odd).arg(a.size())));
        QVERIFY2(worst < 20.0,
                 qPrintable(QString("worst pitch difference %1 cents")
                            .arg(worst)));

        // And the notes that start within it
        sv::EventVector an = notesIn(wholeNotes, start, end);
        sv::EventVector bn = notesIn(noteEvents(analyser), start, end);
        QCOMPARE(int(an.size()), 1);
        QCOMPARE(bn.size(), an.size());
        for (size_t i = 0; i < an.size(); ++i) {
            QVERIFY2(std::abs(bn[i].getFrame() - an[i].getFrame()) <= 2 * hop,
                     qPrintable(QString("note starts at %1, whole-file run "
                                        "had %2")
                                .arg(bn[i].getFrame()).arg(an[i].getFrame())));
            QVERIFY2(std::abs(bn[i].getDuration() - an[i].getDuration())
                     <= 2 * hop,
                     qPrintable(QString("note lasts %1, whole-file run had %2")
                                .arg(bn[i].getDuration())
                                .arg(an[i].getDuration())));
            QVERIFY2(std::abs(TestSignals::centsBetween(bn[i].getValue(),
                                                        an[i].getValue()))
                     < 20.0,
                     qPrintable(QString("note at %1 Hz, whole-file run had %2")
                                .arg(bn[i].getValue()).arg(an[i].getValue())));
        }
    }

    // A whole-file analysis, then the same audio analysed again over one
    // range: the file must come out of it exactly as a whole-file
    // analysis left it. No hole where the merge window begins, no frame
    // twice, no note split or shortened, nothing moved. Only the middle
    // of the run is merged for the sake of this
    void verifyRangedRunChangesNothing(sv::sv_frame_t start,
                                       sv::sv_frame_t end) {
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t wFrom, wTo;
        mergeWindow(start, end, 0, fileEnd, wFrom, wTo);

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        sv::EventVector wasPitch = pitchEvents(analyser);
        sv::EventVector wasNotes = noteEvents(analyser);
        QCOMPARE(int(wasNotes.size()), 4);
        QVERIFY2(wasPitch.size() > 200, "too few pitch events to test this");

        size_t layers = m_document->getLayers().size();
        size_t models = m_document->getModels().size();

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;
        verifyNothingLeftOver(layers, models);

        comparePitchAcrossFile(wasPitch, pitchEvents(analyser), wFrom, wTo);
        if (QTest::currentTestFailed()) return;

        // Still four notes, each where it was. A note whose onset is in
        // the window is replaced by the run's own, which may sit a hop or
        // two away; one whose onset is outside it is not touched at all
        sv::EventVector isNotes = noteEvents(analyser);
        QVERIFY2(isNotes.size() == wasNotes.size(),
                 qPrintable(QString("the notes %1 became %2 (merge window "
                                    "%3 to %4)").arg(describeNotes(wasNotes))
                            .arg(describeNotes(isNotes))
                            .arg(wFrom).arg(wTo)));
        for (size_t i = 0; i < wasNotes.size(); ++i) {
            sv::sv_frame_t f = wasNotes[i].getFrame();
            sv::sv_frame_t slack = (f >= wFrom && f < wTo) ? 2 * hop : 0;
            QVERIFY2(std::abs(isNotes[i].getFrame() - f) <= slack,
                     qPrintable(QString("note %1 moved from %2 to %3")
                                .arg(i).arg(f).arg(isNotes[i].getFrame())));
            QVERIFY2(std::abs(isNotes[i].getDuration() -
                              wasNotes[i].getDuration()) <= slack,
                     qPrintable(QString("note %1 lasted %2, now %3").arg(i)
                                .arg(wasNotes[i].getDuration())
                                .arg(isNotes[i].getDuration())));
            QVERIFY(std::abs(TestSignals::centsBetween
                             (isNotes[i].getValue(),
                              wasNotes[i].getValue())) < 20.0);
        }
    }

    void ranged_leaves_the_rest_alone() {
        // The third note of fourNotes() analysed again, with both edges of
        // the merge window landing in the silence around it
        verifyRangedRunChangesNothing(frameAt(thirdNoteFrom),
                                      frameAt(thirdNoteTo));
    }

    void ranged_range_shorter_than_the_margin() {
        // One hop asked for, in the middle of the third note: the merge
        // window is half a second wide all the same, and its right edge
        // now falls inside that note (which the run therefore replaces,
        // whole, tail and all -- the note's end is inside the run)
        sv::sv_frame_t start = frameAt(3.0);
        sv::sv_frame_t wFrom, wTo;
        mergeWindow(start, start + hop, 0, frameAt(5.0), wFrom, wTo);
        QVERIFY(wFrom >= frameAt(2.75) && wTo > frameAt(3.25) &&
                wTo < frameAt(3.3)); // the note runs 2.8 to 3.3 s
        verifyRangedRunChangesNothing(start, start + hop);
    }

    void ranged_keeps_a_note_across_the_window_edge() {
        // A note that runs into the merge window from before it: the run
        // finds it going on from before the window too, so it has no note
        // to add inside the window, and the one note stays one note --
        // where it used to be cut in two
        std::vector<float> data;
        appendSilence(data, 0.3);
        auto lng = tone(singingHz, 2.0);
        data.insert(data.end(), lng.begin(), lng.end());
        appendSilence(data, 0.3);
        auto second = tone(referenceHz, 0.5);
        data.insert(data.end(), second.begin(), second.end());
        appendSilence(data, 0.3);

        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t start = frameAt(1.5), end = frameAt(2.0);
        sv::sv_frame_t wFrom, wTo;
        mergeWindow(start, end, 0, fileEnd, wFrom, wTo);

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        // One long note from 0.3 to 2.3 s, and a short one after it
        sv::EventVector wasNotes = noteEvents(analyser);
        sv::EventVector wasPitch = pitchEvents(analyser);
        QCOMPARE(int(wasNotes.size()), 2);
        sv::Event crossing = wasNotes[0];
        QVERIFY2(crossing.getFrame() < wFrom &&
                 crossing.getFrame() + crossing.getDuration() > wTo,
                 qPrintable(QString("the long note (%1 for %2) does not cross "
                                    "the merge window %3 to %4")
                            .arg(crossing.getFrame())
                            .arg(crossing.getDuration())
                            .arg(wFrom).arg(wTo)));

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        // The run begins in the middle of the note, where it cannot stamp
        // its first two hops: the pitch track of the whole file must come
        // through it without a hole all the same
        comparePitchAcrossFile(wasPitch, pitchEvents(analyser), wFrom, wTo);
        if (QTest::currentTestFailed()) return;

        sv::EventVector now = noteEvents(analyser);
        QVERIFY2(now.size() == wasNotes.size() &&
                 now[0].getFrame() == crossing.getFrame() &&
                 now[0].getDuration() == crossing.getDuration(),
                 qPrintable(QString("the notes %1 became %2 (merge window "
                                    "%3 to %4)").arg(describeNotes(wasNotes))
                            .arg(describeNotes(now)).arg(wFrom).arg(wTo)));

        // and the note after the range is untouched
        QCOMPARE(now.back().getFrame(), wasNotes[1].getFrame());
        QCOMPARE(now.back().getDuration(), wasNotes[1].getDuration());
    }

    void ranged_cuts_a_new_note_at_the_next_old_note() {
        // A new note may not run over an old note that begins at or after
        // the window: the old onset stands and the new note is cut back to
        // it. The old notes here are put in by hand, as notes that the
        // user has edited or that belong to material the swap replaced
        std::vector<float> data;
        appendSilence(data, 1.4);
        auto lng = tone(singingHz, 1.6); // 1.4 to 3.0 s
        data.insert(data.end(), lng.begin(), lng.end());
        appendSilence(data, 0.3);

        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t start = frameAt(1.5), end = frameAt(2.0);
        sv::sv_frame_t wFrom, wTo;
        mergeWindow(start, end, 0, fileEnd, wFrom, wTo);
        QVERIFY(wFrom < frameAt(1.4) && wTo > frameAt(2.2));

        Analyser analyser(Analyser::SecondaryColors);
        setUpEmpty(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;
        auto notes = notesModel(analyser);
        QVERIFY(notes);

        // Before the window, inside it, and one beginning after it that
        // the run's note will want to run over
        sv::Event before(frameAt(0.2), 200.f, frameAt(0.3), "before");
        sv::Event inside(frameAt(1.6), 200.f, frameAt(0.3), "inside");
        sv::Event after(wTo + frameAt(0.05), 200.f, frameAt(0.6), "after");
        for (const auto &e : { before, inside, after }) notes->add(e);

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        sv::EventVector now = noteEvents(analyser);
        QVERIFY2(now.size() == 3,
                 qPrintable("notes after the merge: " + describeNotes(now)));

        // The note before the window is untouched: no new note starts
        // inside it
        QCOMPARE(now[0].getFrame(), before.getFrame());
        QCOMPARE(now[0].getDuration(), before.getDuration());

        // The one inside it is gone, replaced by the run's note, which
        // starts near the start of the tone and is cut back to the onset
        // of the note after the window
        QVERIFY2(std::abs(now[1].getFrame() - frameAt(1.4)) <= 4 * hop,
                 qPrintable(QString("the run's note starts at %1, the tone at "
                                    "%2").arg(now[1].getFrame())
                            .arg(frameAt(1.4))));
        QCOMPARE(now[1].getFrame() + now[1].getDuration(), after.getFrame());
        QVERIFY(std::abs(TestSignals::centsBetween(now[1].getValue(),
                                                   singingHz)) < 50.0);

        // and the note after the window keeps its onset and its length
        QCOMPARE(now[2].getFrame(), after.getFrame());
        QCOMPARE(now[2].getDuration(), after.getDuration());
    }

    void ranged_keeps_the_end_of_a_note_past_the_run() {
        // A note that begins inside the merge window and is still going
        // when the run ends: the run had to cut it off where it stopped
        // listening, but the audio out there has not changed, so the note
        // ends where the old note that covered the run's end ended
        std::vector<float> data;
        appendSilence(data, 1.4);
        auto lng = tone(singingHz, 2.4);      // 1.4 to 3.8 s
        data.insert(data.end(), lng.begin(), lng.end());
        appendSilence(data, 0.3);

        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t start = frameAt(1.5), end = frameAt(2.0);
        sv::sv_frame_t from, to, wFrom, wTo;
        widenRange(start, end, 0, fileEnd, from, to);
        mergeWindow(start, end, 0, fileEnd, wFrom, wTo);
        // The note begins inside the window and outlasts the run
        QVERIFY(wFrom < frameAt(1.4) && wTo > frameAt(1.4));
        QVERIFY(to < frameAt(3.5));

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        sv::EventVector wasNotes = noteEvents(analyser);
        QCOMPARE(int(wasNotes.size()), 1);
        sv::sv_frame_t wasEnd =
            wasNotes[0].getFrame() + wasNotes[0].getDuration();
        QVERIFY2(wasEnd > to,
                 qPrintable(QString("the long note (%1 for %2) does not "
                                    "outlast the run, which ends at %3")
                            .arg(wasNotes[0].getFrame())
                            .arg(wasNotes[0].getDuration()).arg(to)));

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        sv::EventVector now = noteEvents(analyser);
        QVERIFY2(now.size() == 1,
                 qPrintable("notes after the merge: " + describeNotes(now)));
        QVERIFY(std::abs(now[0].getFrame() - wasNotes[0].getFrame()) <=
                4 * hop);
        QVERIFY2(now[0].getFrame() + now[0].getDuration() == wasEnd,
                 qPrintable(QString("the note %1 was cut off at the end of "
                                    "the run (%2); it used to end at %3")
                            .arg(describeNotes(now)).arg(to).arg(wasEnd)));
    }

    void ranged_join_inside_a_note_data() {
        QTest::addColumn<bool>("sameNote");
        QTest::newRow("the same note going on") << true;
        QTest::newRow("a new note from the join") << false;
    }

    void ranged_join_inside_a_note() {
        // Two punch-ins meeting at J, as MainWindow analyses them: the
        // first's range alone, its coverage ending at J, then the second's
        // with the coverage grown to take it in. The second's run starts
        // half a second before J, inside the first's note, so the note it
        // finds there begins before the merge window. When the singer
        // holds the note through J, that is the first's note going on, and
        // the two are one. When a different note begins at J, they are two:
        // the first's note must not be carried on over the new one
        QFETCH(bool, sameNote);

        std::vector<float> data;
        appendSilence(data, 0.2);
        auto held = tone(singingHz, 1.3);            // 0.2 to 1.5 s
        data.insert(data.end(), held.begin(), held.end());
        auto then = tone(sameNote ? singingHz : referenceHz, 1.3);
        data.insert(data.end(), then.begin(), then.end()); // to 2.8 s
        appendSilence(data, 0.3);

        const sv::sv_frame_t P = frameAt(0.5), J = frameAt(1.5),
            E = frameAt(2.5);
        sv::sv_frame_t wFrom, wTo;
        mergeWindow(J, E, P, E, wFrom, wTo);
        QVERIFY(wFrom > P && wFrom < J);

        Analyser analyser(Analyser::SecondaryColors);
        setUpEmpty(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(P, J, P, J), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        // The first punch-in's note, running from near P to J
        sv::EventVector first = noteEvents(analyser);
        QVERIFY2(first.size() == 1 &&
                 std::abs(first[0].getFrame() - P) <= 4 * hop &&
                 std::abs(first[0].getFrame() + first[0].getDuration() - J)
                 <= 4 * hop, qPrintable("first: " + describeNotes(first)));

        done.clear();
        QCOMPARE(analyser.analyseRange(J, E, P, E), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        sv::EventVector now = noteEvents(analyser);
        QString what = QString("the first punch-in's note %1 and the second's "
                               "(merge window %2 to %3) became %4")
            .arg(describeNotes(first)).arg(wFrom).arg(wTo)
            .arg(describeNotes(now));

        // The first's note keeps its onset and pitch either way
        QVERIFY2(!now.empty() && now[0].getFrame() == first[0].getFrame() &&
                 now[0].getValue() == first[0].getValue(), qPrintable(what));
        sv::sv_frame_t end = now[0].getFrame() + now[0].getDuration();

        if (sameNote) {
            // and runs on through J to the end of the coverage
            QVERIFY2(now.size() == 1 && std::abs(end - E) <= 4 * hop,
                     qPrintable(what));
        } else {
            // and still ends at J, where the new note begins, sung at its
            // own pitch
            QVERIFY2(now.size() == 2 && std::abs(end - J) <= 4 * hop &&
                     now[1].getFrame() >= end &&
                     std::abs(now[1].getFrame() - J) <= 4 * hop &&
                     std::abs(TestSignals::centsBetween
                              (now[1].getValue(), referenceHz)) < 50.0,
                     qPrintable(what));
        }
    }

    void ranged_at_the_edge_of_coverage() {
        // Where the caller's coverage limit clips an edge of the run there
        // is nothing beyond it but silence and no context worth keeping,
        // so the window reaches that edge and takes in everything the run
        // stamped there. Three cases: both edges clipped (the first
        // recording of a take, with nothing in the models yet), the left
        // edge, the right edge
        std::vector<float> data;
        appendSilence(data, 0.15);
        auto first = tone(singingHz, 0.5);   // 0.15 to 0.65 s
        data.insert(data.end(), first.begin(), first.end());
        appendSilence(data, 0.4);
        auto second = tone(referenceHz, 0.5); // 1.05 to 1.55 s
        data.insert(data.end(), second.begin(), second.end());
        appendSilence(data, 0.45);            // silence beyond the coverage

        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t coverEnd = frameAt(1.55);

        sv::EventVector wholePitch, wholeNotes;
        {
            Analyser whole(Analyser::SecondaryColors);
            analyse(whole, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            wholePitch = pitchEvents(whole);
            wholeNotes = noteEvents(whole);
            whole.removeAllLayers();
        }
        QCOMPARE(int(wholeNotes.size()), 2);

        // Both edges: empty models, the coverage is the range recorded,
        // and the run is the whole of it. Nothing may be dropped at
        // either end, so the result is the whole-file result
        {
            Analyser analyser(Analyser::SecondaryColors);
            setUpEmpty(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
            QCOMPARE(analyser.analyseRange(0, coverEnd, 0, coverEnd),
                     QString());
            waitForRange(analyser, done);
            if (QTest::currentTestFailed()) return;

            auto a = byFrame(wholePitch), b = byFrame(pitchEvents(analyser));
            QStringList odd;
            for (const auto &p : a) if (!b.count(p.first)) odd << QString("-%1").arg(p.first);
            for (const auto &p : b) if (!a.count(p.first)) odd << QString("+%1").arg(p.first);
            QVERIFY2(odd.isEmpty(),
                     qPrintable("pitch frames lost (-) or gained (+) by the "
                                "run over the whole coverage: " +
                                odd.join(" ")));
            sv::EventVector notes = noteEvents(analyser);
            QVERIFY2(notes.size() == wholeNotes.size(),
                     qPrintable("notes " + describeNotes(wholeNotes) +
                                " became " + describeNotes(notes)));
            for (size_t i = 0; i < notes.size(); ++i) {
                QVERIFY(std::abs(notes[i].getFrame() -
                                 wholeNotes[i].getFrame()) <= 2 * hop);
            }
            // or the next analyser would claim these layers as its own
            analyser.removeAllLayers();
        }

        // The left edge only: a range at the very start of the coverage
        // still gets its first note, and nothing before it is lost
        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            sv::EventVector wasPitch = pitchEvents(analyser);
            sv::sv_frame_t start = frameAt(0.1), end = frameAt(0.7);
            sv::sv_frame_t wFrom, wTo;
            mergeWindow(start, end, 0, fileEnd, wFrom, wTo);
            QCOMPARE(wFrom, sv::sv_frame_t(0)); // clipped to nothing

            QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
            QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
            waitForRange(analyser, done);
            if (QTest::currentTestFailed()) return;

            sv::EventVector notes = noteEvents(analyser);
            QVERIFY2(notes.size() == 2,
                     qPrintable("notes after the merge: " +
                                describeNotes(notes)));
            QVERIFY(std::abs(notes[0].getFrame() -
                             wholeNotes[0].getFrame()) <= 2 * hop);
            comparePitchAcrossFile(wasPitch, pitchEvents(analyser), wFrom, wTo);
            if (QTest::currentTestFailed()) return;
            analyser.removeAllLayers();
        }

        // The right edge only: the run stops at the end of the coverage,
        // and what it stamped past that end (pYIN stamps a block two hops
        // in) is kept, so the last of the pitch track is not lost
        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            sv::EventVector wasPitch = pitchEvents(analyser);
            sv::sv_frame_t start = frameAt(1.2), end = coverEnd;
            sv::sv_frame_t wFrom, wTo;
            mergeWindow(start, end, 0, coverEnd, wFrom, wTo);
            QVERIFY(wTo >= coverEnd);

            QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
            QCOMPARE(analyser.analyseRange(start, end, 0, coverEnd), QString());
            waitForRange(analyser, done);
            if (QTest::currentTestFailed()) return;

            sv::EventVector notes = noteEvents(analyser);
            QVERIFY2(notes.size() == 2,
                     qPrintable("notes after the merge: " +
                                describeNotes(notes)));
            QVERIFY(std::abs(notes[1].getFrame() -
                             wholeNotes[1].getFrame()) <= 2 * hop);
            // The window reaches past the end of the run here, so the
            // comparison allows for the run's own last events
            comparePitchAcrossFile(wasPitch, pitchEvents(analyser),
                                   wFrom, wTo + 4 * hop);
        }
    }

    void ranged_cancelled() {
        // Closing, re-recording or switching take while a ranged
        // analysis runs goes through cancelAnalyses()
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        sv::EventVector wasPitch = pitchEvents(analyser);
        sv::EventVector wasNotes = noteEvents(analyser);
        size_t layers = m_document->getLayers().size();
        size_t models = m_document->getModels().size();

        int inPane = m_pane->getLayerCount();
        sv::Layer *current = m_pane->getSelectedLayer();

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(0, fileEnd, 0, fileEnd), QString());
        QVERIFY2(analyser.isAnalysingRange(),
                 "the whole file was analysed before we could cancel it");
        QVERIFY(m_document->getLayers().size() > layers);

        // The temporary layers are in the document but in no view, so
        // nothing shows them, nothing selects them, and the scan for
        // existing analyses (which looks in the pane) cannot take them
        // for the analyser's own
        QCOMPARE(m_pane->getLayerCount(), inPane);
        QVERIFY(m_pane->getSelectedLayer() == current);

        analyser.cancelAnalyses();

        QVERIFY(!analyser.isAnalysingRange());
        verifyNothingLeftOver(layers, models);

        // and no late merge from the transform we abandoned
        QTest::qWait(500);
        QCOMPARE(done.count(), 0);
        verifyNothingLeftOver(layers, models);
        QCOMPARE(pitchEvents(analyser).size(), wasPitch.size());
        QCOMPARE(noteEvents(analyser).size(), wasNotes.size());
        for (size_t i = 0; i < wasPitch.size(); ++i) {
            QCOMPARE(pitchEvents(analyser)[i].getFrame(),
                     wasPitch[i].getFrame());
        }
    }

    void ranged_released_while_running() {
        // releaseLayers() (the model swap of 4a) and the destructor must
        // take the temporaries with them too
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());

        size_t layers = 0, models = 0;
        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            layers = m_document->getLayers().size();
            models = m_document->getModels().size();

            QCOMPARE(analyser.analyseRange(0, fileEnd, 0, fileEnd), QString());
            QVERIFY(analyser.isAnalysingRange());
            analyser.releaseLayers();
            QVERIFY(!analyser.isAnalysingRange());

            // releaseLayers() deletes the waveform layer and its model
            QCOMPARE(m_document->getLayers().size(), layers - 1);
        }

        // A second run, abandoned by the destructor this time
        {
            Analyser analyser(Analyser::SecondaryColors);
            analyse(analyser, addSingingModel(data));
            if (QTest::currentTestFailed()) return;
            layers = m_document->getLayers().size();
            models = m_document->getModels().size();

            QCOMPARE(analyser.analyseRange(0, fileEnd, 0, fileEnd), QString());
            QVERIFY(analyser.isAnalysingRange());
        }
        verifyNothingLeftOver(layers, models);
    }

    void ranged_restarted_while_running() {
        // A second call abandons the first: its material is presumed to
        // have changed under us
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t firstStart = frameAt(0.5), firstEnd = frameAt(1.2);
        sv::sv_frame_t start = frameAt(thirdNoteFrom);
        sv::sv_frame_t end = frameAt(thirdNoteTo);
        sv::sv_frame_t abandoned0, abandoned1, from, to;
        widenRange(firstStart, firstEnd, 0, fileEnd, abandoned0, abandoned1);
        widenRange(start, end, 0, fileEnd, from, to);
        QVERIFY(abandoned1 < from); // the two ranges do not meet

        Analyser analyser(Analyser::SecondaryColors);
        setUpEmpty(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        size_t layers = m_document->getLayers().size();
        size_t models = m_document->getModels().size();

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(firstStart, firstEnd, 0, fileEnd),
                 QString());
        QVERIFY(analyser.isAnalysingRange());
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        QVERIFY(analyser.isAnalysingRange());

        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(done.count(), 1);
        verifyNothingLeftOver(layers, models);

        // Only the second range was merged
        auto events = pitchEvents(analyser);
        QVERIFY(events.size() > 60);
        for (const auto &e : events) {
            QVERIFY2(e.getFrame() >= from && e.getFrame() < to + 2 * hop,
                     qPrintable(QString("pitch event at %1, from the range "
                                        "that was abandoned").arg(e.getFrame())));
        }
        QCOMPARE(int(notesIn(noteEvents(analyser), abandoned0, abandoned1).size()),
                 0);
        QVERIFY(notesIn(noteEvents(analyser), from, to + 2 * hop).size() >= 1);
    }
};

#endif
