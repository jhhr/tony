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

#ifndef TEST_HOTKEYS_H
#define TEST_HOTKEYS_H

// Tier 1: the hotkeys chosen, as the settings keep them, and the keys
// two commands have

#include "../Hotkeys.h"

#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestHotkeys : public QObject
{
    Q_OBJECT

private slots:
    void init() {
        QSettings settings;
        Hotkeys::forgetAll(settings);
    }

    void cleanup() {
        QSettings settings;
        Hotkeys::forgetAll(settings);
    }

    // A command's keys are its defaults until others are chosen, and
    // none once none are
    void defaults_then_chosen_then_none() {
        QSettings settings;
        const QStringList defaults { "Esc", "Ctrl+Esc" };
        QCOMPARE(Hotkeys::keys(settings, "clear-selection", defaults),
                 defaults);

        Hotkeys::setKeys(settings, "clear-selection", { "Ctrl+K" }, defaults);
        QCOMPARE(Hotkeys::keys(settings, "clear-selection", defaults),
                 QStringList({ "Ctrl+K" }));

        Hotkeys::setKeys(settings, "clear-selection", {}, defaults);
        QCOMPARE(Hotkeys::keys(settings, "clear-selection", defaults),
                 QStringList());
        settings.beginGroup(Hotkeys::kGroup);
        QVERIFY(settings.contains("clear-selection"));
        settings.endGroup();
    }

    // The defaults chosen again keep nothing, so that new defaults in a
    // later build reach a command the user never changed
    void the_defaults_keep_nothing() {
        QSettings settings;
        Hotkeys::setKeys(settings, "play", { "P" }, { "Space" });
        Hotkeys::setKeys(settings, "play", { "Space" }, { "Space" });
        settings.beginGroup(Hotkeys::kGroup);
        QVERIFY(!settings.contains("play"));
        settings.endGroup();
        QCOMPARE(Hotkeys::keys(settings, "play", { "Space" }),
                 QStringList({ "Space" }));
    }

    void forget_all() {
        QSettings settings;
        Hotkeys::setKeys(settings, "play", { "P" }, { "Space" });
        Hotkeys::setKeys(settings, "loop", {}, { "L" });
        Hotkeys::forgetAll(settings);
        QCOMPARE(Hotkeys::keys(settings, "play", { "Space" }),
                 QStringList({ "Space" }));
        QCOMPARE(Hotkeys::keys(settings, "loop", { "L" }),
                 QStringList({ "L" }));
    }

    // Read back from a file, none is still none and not the defaults
    void none_survives_a_file() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("hotkeys.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            Hotkeys::setKeys(settings, "loop", {}, { "L" });
            Hotkeys::setKeys(settings, "clear-selection",
                             { "Ctrl+;", "Ctrl+X" }, { "Esc", "Ctrl+Esc" });
        }
        QSettings settings(path, QSettings::IniFormat);
        QCOMPARE(Hotkeys::keys(settings, "loop", { "L" }), QStringList());
        QCOMPARE(Hotkeys::keys(settings, "clear-selection",
                               { "Esc", "Ctrl+Esc" }),
                 QStringList({ "Ctrl+;", "Ctrl+X" }));
        QCOMPARE(Hotkeys::keys(settings, "play", { "Space" }),
                 QStringList({ "Space" }));
    }

    // As QKeySequence::listToString() and listFromString() have them,
    // a key with a semicolon in it included
    void joined_and_split() {
        QCOMPARE(Hotkeys::joined({ "Esc", "Ctrl+Esc" }),
                 QString("Esc; Ctrl+Esc"));
        QCOMPARE(Hotkeys::split("Esc; Ctrl+Esc"),
                 QStringList({ "Esc", "Ctrl+Esc" }));
        QCOMPARE(Hotkeys::split(""), QStringList());
        QCOMPARE(Hotkeys::joined({}), QString());
        QCOMPARE(Hotkeys::split(Hotkeys::joined({ "Ctrl+;", "Ctrl+X" })),
                 QStringList({ "Ctrl+;", "Ctrl+X" }));
    }

    void no_clashes() {
        QVERIFY(Hotkeys::clashes({ { "play", { "Space" } },
                                   { "loop", { "L" } },
                                   { "none", {} } }).empty());
    }

    // Each of two commands with one key names the other; a third with
    // the same key names both; a command listing its own key twice
    // clashes with nothing; no key, "", is no clash
    void clashes_name_the_others() {
        std::map<QString, QStringList> c = Hotkeys::clashes
            ({ { "play", { "Space" } },
               { "loop", { "L", "Space" } },
               { "twice", { "T", "T" } },
               { "empty", { "" } },
               { "empty2", { "" } } });
        QCOMPARE(int(c.size()), 2);
        QCOMPARE(c["play"], QStringList({ "loop" }));
        QCOMPARE(c["loop"], QStringList({ "play" }));

        c = Hotkeys::clashes({ { "a", { "S" } }, { "b", { "X" } },
                               { "c", { "S" } }, { "d", { "S" } } });
        QCOMPARE(int(c.size()), 3);
        QCOMPARE(c["a"], QStringList({ "c", "d" }));
        QCOMPARE(c["c"], QStringList({ "a", "d" }));
        QCOMPARE(c["d"], QStringList({ "a", "c" }));
    }
};

#endif
