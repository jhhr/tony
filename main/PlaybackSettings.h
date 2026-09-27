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

#ifndef TONY_PLAYBACK_SETTINGS_H
#define TONY_PLAYBACK_SETTINGS_H

#include <QString>

class QSettings;

/**
 * How the user has the tracks of the "Show and Play" toolbar shown and
 * played, and the master volume, as the settings keep them between
 * launches.
 *
 * A track is a component of an analyser (Analyser::Component, passed
 * here as an int: Audio 0, PitchTrack 1, Notes 2, Spectrogram 3), and
 * each analyser keeps its tracks in a group of its own, so that the
 * singing toggles never change what the reference shows or plays, nor
 * the other way round.  In the group, "visible-N", "audible-N", "gain-N"
 * and "pan-N" for track N.  Whoever reads a key says what it is when
 * the user has never set it.
 *
 * The master volume and the background music belong to no analyser:
 * they are the window's, and what they are when never set is fixed
 * here.
 */
namespace PlaybackSettings
{
    /// The reference's tracks.  The analysis options
    /// (Analyser::getAnalysisSettings()) are in this group as well, and
    /// are the singing analyser's too
    constexpr const char *kReferenceGroup = "Analyser";

    /// The singing track's
    constexpr const char *kSingingGroup = "SingingAnalyser";

    bool visible(QSettings &settings, QString group, int component,
                 bool byDefault);
    void setVisible(QSettings &settings, QString group, int component,
                    bool visible);

    bool audible(QSettings &settings, QString group, int component,
                 bool byDefault);
    void setAudible(QSettings &settings, QString group, int component,
                    bool audible);

    /// The play gain, as PlayParameters has it (1 is the level the
    /// audio was made at)
    double gain(QSettings &settings, QString group, int component,
                double byDefault);
    void setGain(QSettings &settings, QString group, int component,
                 double gain);

    /// The play pan, as PlayParameters has it (-1 left, 1 right)
    double pan(QSettings &settings, QString group, int component,
               double byDefault);
    void setPan(QSettings &settings, QString group, int component,
                double pan);

    /// The window's, with its other options
    constexpr const char *kWindowGroup = "MainWindow";

    /// The master volume: the Playback Controls fader's value, which the
    /// device gets as its output gain.  1 (the mix as it is) if never set
    double masterVolume(QSettings &settings);
    void setMasterVolume(QSettings &settings, double volume);

    /// Mix Background Music.  On if never set
    bool backgroundMusicMix(QSettings &settings);
    void setBackgroundMusicMix(QSettings &settings, bool mix);

    /// The background music's play gain and pan, as PlayParameters has
    /// them.  1 and 0 (the middle) if never set
    double backgroundMusicGain(QSettings &settings);
    void setBackgroundMusicGain(QSettings &settings, double gain);
    double backgroundMusicPan(QSettings &settings);
    void setBackgroundMusicPan(QSettings &settings, double pan);
}

#endif
