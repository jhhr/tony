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

#ifndef TONY_AUDIO_DRIVER_MENUS_H
#define TONY_AUDIO_DRIVER_MENUS_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QMenu;
class QActionGroup;

/**
 * Playback > Audio Driver and Audio Latency: the driver the device is
 * opened through (MME, DirectSound, WASAPI) and the latency asked of it,
 * each kept in the Preferences (AudioDriverSettings).  Both are shown
 * only where more than one driver is built in, which is on Windows.
 *
 * A choice is written to the Preferences here, and then said with
 * driverChosen() or latencyChosen(): MainWindow stops what is playing
 * and opens the device again, which is where the choice takes effect.
 * Before it opens one, it hands the latency chosen for the driver to
 * bqaudioio with applyLatency().
 */
class AudioDriverMenus : public QObject
{
    Q_OBJECT

public:
    /**
     * Add the two submenus to the end of the menu given.  The list gives
     * the implementations bqaudioio has, whenever the menus are built.
     */
    AudioDriverMenus(QMenu *menu, std::function<QStringList()> implementations,
                     QObject *parent = nullptr);
    virtual ~AudioDriverMenus();

    QMenu *driverMenu() const { return m_driverMenu; }
    QMenu *latencyMenu() const { return m_latencyMenu; }

    /**
     * Build both again from the implementations there are and the
     * Preferences: shown if there are two drivers or more, with the
     * driver named and its latency ticked.  Done as either opens.
     */
    void rebuild();

    /// Not while a take is being recorded or an audio check runs
    void setEnabled(bool enabled);

    /**
     * Hand bqaudioio the latency chosen for the driver the Preferences
     * name, for the streams opened from now on.  Returns it.
     */
    double applyLatency();

    /// What applyLatency() last handed over, in seconds; 0 before it has
    double appliedLatency() const { return m_appliedLatency; }

    /// A driver's name as the user knows it: "WASAPI" for "wasapi"
    static QString driverName(QString implementation);

signals:
    void driverChosen(QString implementation);
    void latencyChosen(double seconds);

private:
    QMenu *m_driverMenu;
    QActionGroup *m_driverGroup;
    QMenu *m_latencyMenu;
    QActionGroup *m_latencyGroup;
    std::function<QStringList()> m_implementations;
    double m_appliedLatency;

    void driverTriggered(QString implementation);
    void latencyTriggered(double seconds);
};

#endif
