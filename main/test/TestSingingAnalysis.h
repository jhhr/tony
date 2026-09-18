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
#include "base/PlayParameters.h"

#include <QObject>
#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
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
        QEXPECT_FAIL("", "Review finding 13: SVFileReader does not re-apply "
                     "\"start\" to wave file models", Continue);
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
};

#endif
