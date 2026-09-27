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

#ifndef TONY_AUDIO_CHECK_INDICATOR_H
#define TONY_AUDIO_CHECK_INDICATOR_H

#include <QWidget>

class QLabel;
class QProgressBar;

/**
 * The Calibrate Audio dialog made small while its check runs: a bar and
 * one line of text (the step, the punch-in, the time left), which the
 * window puts at the right end of its status bar, below the panes, so
 * that the check's takes can be watched as they are drawn. A tap or a
 * click on it says clicked(), and the dialog comes back with Cancel.
 *
 * The line keeps one width whatever it says, so that the status bar
 * does not jump at each step, and is cut short with an ellipsis where
 * it is longer; the whole of it is in the tooltip.
 */
class AudioCheckIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit AudioCheckIndicator(QWidget *parent = nullptr);
    virtual ~AudioCheckIndicator();

    void setText(QString text);
    QString text() const { return m_text; }

    /// How far the run has got, from 0 to 1000; -1 while that is not
    /// known, for a bar that says only that something is going on
    void setProgress(int permille);
    int progress() const;

    /// The line's width, in average characters of the font
    static const int textWidth = 40;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    QString m_text;
    QLabel *m_label;
    QProgressBar *m_bar;
    bool m_pressed;

    void updateSizes();
    void updateLabel();
};

#endif
