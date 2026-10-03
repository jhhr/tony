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

#ifndef TONY_INPUT_CHANNEL_H
#define TONY_INPUT_CHANNEL_H

#include <QString>

#include <vector>

class QSettings;

/**
 * The input channel a take is made from: both inputs of the device as
 * they come, or one of them alone, for a microphone on one input of an
 * interface with two.  With both, a stereo device gives a stereo take
 * whose levels are judged as the channels' average, which reads a
 * microphone on one input 6 dB under its own level; with one, the take
 * is mono, made from that input, and the live dots, the analysis and
 * the voice threshold hear that input at its own level
 * (docs/recording.md, "Input channels").
 *
 * A channel is counted from 0, as a file's are: Input 1 is channel 0.
 *
 * Kept per input device, as the other things kept per device are: in
 * the group "InputChannel", under the driver and the record device as
 * LatencyCalibration names them (the Preferences' names on a desktop,
 * the route's input on a phone), as the input number, 1 or 2.  Both is
 * no key at all.
 */
namespace InputChannel
{
    /// Every input as it comes: what a take was made of before there
    /// was a choice, and what is read where none is kept
    constexpr int kBoth = -1;

    /// The input device a choice is kept for
    struct Key {
        /// The driver, as the Preferences or the route name it; "" for
        /// none
        QString driver;

        /// The record device, as the Preferences or the route name it;
        /// "" for the default device, or one not known
        QString recordDevice;
    };

    /// The choices offered: Both, Input 1, Input 2
    std::vector<int> choices();

    /// "Both Inputs", "Input 1", "Input 2"
    QString label(int channel);

    /// The choice kept for the device, or kBoth where none is, or where
    /// the one kept is not one of the choices
    int channel(QSettings &settings, const Key &key);

    void setChannel(QSettings &settings, const Key &key, int channel);

    /**
     * The input a phone last recorded from with this driver: what a
     * choice applies to while the device is open for playback only and
     * cannot say which input it will record from.  "" if none
     */
    QString lastInput(QSettings &settings, QString driver);

    void setLastInput(QSettings &settings, QString driver, QString input);

    /**
     * The channel to make a take from, of a recording with that many
     * channels: the one chosen if the recording has it, else kBoth, as
     * for a device with one input, which is the same whichever is
     * chosen
     */
    int effectiveChannel(int chosen, int channels);

    /**
     * Whether a choice makes the take from one input alone: what a
     * phone opens its input at the device's own channels for, instead of
     * the one channel Android would make of them
     */
    inline bool isSingle(int channel) { return channel >= 0; }

    /**
     * The peak a meter shows of the peaks a device reports for its first
     * two inputs, left and right, for a choice: the input chosen, or the
     * louder of the two.  A device with one input reports it as both
     */
    float peakOf(int channel, float left, float right);
}

#endif
