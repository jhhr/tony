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

#include "AudioCheckIndicator.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include "PopupArea.h"
#include <QGuiApplication>
#include <QScreen>
#endif

AudioCheckIndicator::AudioCheckIndicator(QWidget *parent) :
    QWidget(parent),
    m_pressed(false)
{
    QHBoxLayout *layout = new QHBoxLayout;
    layout->setContentsMargins(4, 0, 4, 0);
    setLayout(layout);

    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_label = new QLabel;
    layout->addWidget(m_bar);
    layout->addWidget(m_label);

    // The whole of it is the one thing to press
    m_bar->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    setCursor(Qt::PointingHandCursor);

#ifdef Q_OS_ANDROID
    // Tall enough for a finger at the bottom of the screen: the status
    // bar grows by it while the check runs
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        setMinimumHeight(PopupArea::fingerWidth
                         (screen->physicalDotsPerInchY()) * 2 / 3);
    }
#endif

    updateSizes();
}

AudioCheckIndicator::~AudioCheckIndicator()
{
}

void
AudioCheckIndicator::setText(QString text)
{
    m_text = text;
    setToolTip(tr("%1. Show the check, with Cancel").arg(text));
    updateLabel();
}

void
AudioCheckIndicator::setProgress(int permille)
{
    if (permille < 0) {
        m_bar->setRange(0, 0);
        return;
    }
    m_bar->setRange(0, 1000);
    m_bar->setValue(std::min(permille, 1000));
}

int
AudioCheckIndicator::progress() const
{
    if (m_bar->maximum() == 0) return -1;
    return m_bar->value();
}

void
AudioCheckIndicator::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_pressed = true;
    e->accept();
}

void
AudioCheckIndicator::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_pressed) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    m_pressed = false;
    e->accept();
    // As a button: a press that slides off and lifts elsewhere is not one
    if (rect().contains(e->position().toPoint())) emit clicked();
}

void
AudioCheckIndicator::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange) updateSizes();
}

void
AudioCheckIndicator::updateSizes()
{
    // The label's font, which a phone may give labels of their own
    const int character = m_label->fontMetrics().averageCharWidth();
    m_bar->setFixedWidth(character * 10);
    m_label->setFixedWidth(character * textWidth);
    updateLabel();
}

void
AudioCheckIndicator::updateLabel()
{
    m_label->setText(m_label->fontMetrics().elidedText
                     (m_text, Qt::ElideRight, m_label->width()));
}
