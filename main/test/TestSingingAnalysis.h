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

    static sv::EventVector noteEvents(Analyser &analyser) {
        sv::Layer *layer = analyser.getLayer(Analyser::Notes);
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::NoteModel>(layer->getModel());
        if (!model) return {};
        return model->getAllEvents();
    }

    // The singing track as 4c will set it up for the first recording of
    // a take: empty pitch and notes layers on the singing model, which a
    // deferred analyser claims without analysing anything (4a)
    void addEmptyAnalyses(sv::ModelId singing) {
        for (auto type : { sv::LayerFactory::TimeValues,
                           sv::LayerFactory::FlexiNotes }) {
            sv::Layer *layer = m_document->createEmptyLayer(type);
            QVERIFY(layer);
            auto model = sv::ModelById::get(layer->getModel());
            QVERIFY(model);
            model->setSourceModel(singing);
            m_document->addLayerToView(m_pane, layer);
        }
    }

    // Wait for a ranged analysis to be merged
    void waitForRange(Analyser &analyser, QSignalSpy &done) {
        QVERIFY2(done.count() > 0 || done.wait(30000),
                 "the ranged analysis did not complete within 30 seconds");
        QVERIFY2(!analyser.isAnalysingRange(),
                 "the analyser still says a range is being analysed");
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

    static sv::EventVector outsidePitch(const sv::EventVector &events,
                                        sv::sv_frame_t from,
                                        sv::sv_frame_t to) {
        sv::EventVector out;
        for (const auto &e : events) {
            if (e.getFrame() < from || e.getFrame() >= to) out.push_back(e);
        }
        return out;
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

    // Notes that lie wholly outside [from, to), so that the merge has no
    // business with them at all
    static sv::EventVector outsideNotes(const sv::EventVector &events,
                                        sv::sv_frame_t from,
                                        sv::sv_frame_t to) {
        sv::EventVector out;
        for (const auto &e : events) {
            if (e.getFrame() + e.getDuration() <= from ||
                e.getFrame() >= to) {
                out.push_back(e);
            }
        }
        return out;
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

        sv::ModelId singing = addSingingModel(data);
        addEmptyAnalyses(singing);
        if (QTest::currentTestFailed()) return;

        Analyser analyser(Analyser::SecondaryColors);
        QCOMPARE(analyser.newFileLoaded(m_document, singing, m_paneStack,
                                        m_pane, true), QString());
        QVERIFY(analyser.getLayer(Analyser::PitchTrack));
        QVERIFY(analyser.getLayer(Analyser::Notes));
        QVERIFY(pitchEvents(analyser).empty());
        QVERIFY(noteEvents(analyser).empty());

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

    void ranged_leaves_the_rest_alone() {
        // A whole-file analysis, then the same audio analysed again over
        // one range: outside the widened range not one event may move
        auto data = fourNotes();
        sv::sv_frame_t fileEnd = sv::sv_frame_t(data.size());
        sv::sv_frame_t start = frameAt(thirdNoteFrom);
        sv::sv_frame_t end = frameAt(thirdNoteTo);
        sv::sv_frame_t from, to;
        widenRange(start, end, 0, fileEnd, from, to);

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        sv::EventVector wasPitch = pitchEvents(analyser);
        sv::EventVector wasNotes = noteEvents(analyser);
        QCOMPARE(int(wasNotes.size()), 4);

        size_t layers = m_document->getLayers().size();
        size_t models = m_document->getModels().size();

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;
        verifyNothingLeftOver(layers, models);

        // pYIN stamps a block a quarter of a block in (two hops here), so
        // its events reach a couple of hops past the end of the range
        sv::EventVector wasOut = outsidePitch(wasPitch, from, to + 2 * hop);
        sv::EventVector isOut = outsidePitch(pitchEvents(analyser),
                                             from, to + 2 * hop);
        QVERIFY2(wasOut.size() > 100,
                 "the range covers too much of the file to test this");
        QCOMPARE(isOut.size(), wasOut.size());
        for (size_t i = 0; i < wasOut.size(); ++i) {
            QCOMPARE(isOut[i].getFrame(), wasOut[i].getFrame());
            QCOMPARE(isOut[i].getValue(), wasOut[i].getValue());
        }

        // The notes of the file that lie wholly outside: the first two
        // (0.6 to 1.1 s and 1.7 to 2.2 s) and the last (3.9 to 4.4 s)
        sv::EventVector wasOutN = outsideNotes(wasNotes, from, to + 2 * hop);
        sv::EventVector isOutN = outsideNotes(noteEvents(analyser),
                                              from, to + 2 * hop);
        QCOMPARE(int(wasOutN.size()), 3);
        QCOMPARE(isOutN.size(), wasOutN.size());
        for (size_t i = 0; i < wasOutN.size(); ++i) {
            QCOMPARE(isOutN[i].getFrame(), wasOutN[i].getFrame());
            QCOMPARE(isOutN[i].getDuration(), wasOutN[i].getDuration());
            QCOMPARE(isOutN[i].getValue(), wasOutN[i].getValue());
        }

        // and the third note is still one note, near enough where it was
        sv::EventVector was3 = notesIn(wasNotes, from, to + 2 * hop);
        sv::EventVector is3 = notesIn(noteEvents(analyser), from, to + 2 * hop);
        QCOMPARE(int(was3.size()), 1);
        QCOMPARE(is3.size(), was3.size());
        QVERIFY(std::abs(is3[0].getFrame() - was3[0].getFrame()) <= 2 * hop);
    }

    void ranged_truncates_note_across_the_edge() {
        // A note that runs into the widened range from the left is cut
        // back to its edge; the analysis supplies what is inside
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
        sv::sv_frame_t from, to;
        widenRange(start, end, 0, fileEnd, from, to);

        Analyser analyser(Analyser::SecondaryColors);
        analyse(analyser, addSingingModel(data));
        if (QTest::currentTestFailed()) return;

        // One long note from 0.3 to 2.3 s, and a short one after it
        sv::EventVector wasNotes = noteEvents(analyser);
        QCOMPARE(int(wasNotes.size()), 2);
        sv::Event crossing = wasNotes[0];
        QVERIFY2(crossing.getFrame() < from &&
                 crossing.getFrame() + crossing.getDuration() > from,
                 qPrintable(QString("the long note (%1 for %2) does not cross "
                                    "the edge of the widened range at %3")
                            .arg(crossing.getFrame())
                            .arg(crossing.getDuration()).arg(from)));

        QSignalSpy done(&analyser, SIGNAL(initialAnalysisCompleted()));
        QCOMPARE(analyser.analyseRange(start, end, 0, fileEnd), QString());
        waitForRange(analyser, done);
        if (QTest::currentTestFailed()) return;

        sv::EventVector now = noteEvents(analyser);
        int atCrossing = 0, inRange = 0;
        for (const auto &e : now) {
            if (e.getFrame() == crossing.getFrame()) {
                ++atCrossing;
                QCOMPARE(e.getFrame() + e.getDuration(), from);
                QCOMPARE(e.getValue(), crossing.getValue());
            }
            if (e.getFrame() >= from && e.getFrame() < to + 2 * hop) ++inRange;
        }
        QCOMPARE(atCrossing, 1);
        QVERIFY2(inRange >= 1, "the analysed range came back with no note");

        // The note after the range is untouched
        QCOMPARE(now.back().getFrame(), wasNotes[1].getFrame());
        QCOMPARE(now.back().getDuration(), wasNotes[1].getDuration());
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

        sv::ModelId singing = addSingingModel(data);
        addEmptyAnalyses(singing);
        if (QTest::currentTestFailed()) return;

        Analyser analyser(Analyser::SecondaryColors);
        QCOMPARE(analyser.newFileLoaded(m_document, singing, m_paneStack,
                                        m_pane, true), QString());
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
