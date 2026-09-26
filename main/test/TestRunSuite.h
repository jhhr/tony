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

#ifndef TEST_RUN_SUITE_TEST_H
#define TEST_RUN_SUITE_TEST_H

// Which test functions a shard of a suite runs (TONY_TEST_SHARD): each
// exactly once over the shards, and nothing QTest would not run as a
// test. And the application name a shard's process runs under: one of
// its own, which is what keeps shards running at once apart.

#include "RunSuite.h"

#include <QObject>
#include <QSet>
#include <QtTest>

// A suite as QTest sees one: its test functions, and slots that are not
// test functions. Never run.
class ShardFixture : public QObject
{
    Q_OBJECT

public slots:
    void publicSlot() {}

private slots:
    void initTestCase() {}
    void init() {}
    void first() {}
    void second_data() {}
    void second() {}
    void withArgument(int) {}
    void third() {}
    void fourth() {}
    void fifth() {}
    void cleanup() {}
    void cleanupTestCase() {}
};

class TestRunSuite : public QObject
{
    Q_OBJECT

private slots:
    // One shard is the whole suite, in the order declared
    void one_shard_is_every_test_function() {
        ShardFixture fixture;
        QCOMPARE(shardFunctions(&fixture, 0, 1),
                 QStringList({ "first", "second", "third", "fourth", "fifth" }));
    }

    // Each count-th function from the shard's number, so that the
    // long tests declared together are spread over the shards
    void shards_take_every_nth() {
        ShardFixture fixture;
        QCOMPARE(shardFunctions(&fixture, 0, 2),
                 QStringList({ "first", "third", "fifth" }));
        QCOMPARE(shardFunctions(&fixture, 1, 2),
                 QStringList({ "second", "fourth" }));
    }

    // Over the shards of any count, every test function once
    void shards_cover_each_function_once() {
        ShardFixture fixture;
        QStringList all = shardFunctions(&fixture, 0, 1);
        for (int count = 1; count <= 7; ++count) {
            QStringList seen;
            for (int shard = 0; shard < count; ++shard) {
                seen << shardFunctions(&fixture, shard, count);
            }
            QVERIFY2(seen.size() == all.size() &&
                     QSet<QString>(seen.begin(), seen.end()) ==
                     QSet<QString>(all.begin(), all.end()),
                     qPrintable(QString("%1 shards ran %2")
                                .arg(count).arg(seen.join(", "))));
        }
    }

    // More shards than functions: the ones past the end have none, and
    // runSuite() then does not run the suite at all
    void shards_past_the_end_are_empty() {
        ShardFixture fixture;
        QCOMPARE(shardFunctions(&fixture, 4, 6), QStringList({ "fifth" }));
        QVERIFY(shardFunctions(&fixture, 5, 6).isEmpty());
    }

    // A run that is not sharded keeps the executable's own name, the one
    // meson test and named tests have always used
    void no_shard_is_the_base_name() {
        QCOMPARE(shardApplicationName("test-tony-app", QString()),
                 QString("test-tony-app"));
        QCOMPARE(shardApplicationName("test-tony-app", ""),
                 QString("test-tony-app"));
    }

    // The name says which shard of how many, after the base name
    void a_shard_name_says_which_shard() {
        QCOMPARE(shardApplicationName("test-tony-app", "3/8"),
                 QString("test-tony-app-shard3of8"));
    }

    // The shards of one run are processes at once: none may share a name
    void every_shard_of_a_count_has_its_own_name() {
        for (int count = 1; count <= 8; ++count) {
            QStringList names;
            for (int shard = 0; shard < count; ++shard) {
                names << shardName(shard, count);
            }
            QVERIFY2(QSet<QString>(names.begin(), names.end()).size() == count,
                     qPrintable(QString("%1 shards are named %2")
                                .arg(count).arg(names.join(", "))));
        }
    }

    // Shard i of another count runs other tests, so it is another name
    void the_same_shard_of_another_count_has_another_name() {
        for (int count = 1; count <= 8; ++count) {
            for (int other = count + 1; other <= 8; ++other) {
                for (int shard = 0; shard < count; ++shard) {
                    QString name = shardName(shard, count);
                    QVERIFY2(shardName(shard, other) != name,
                             qPrintable(QString("%1/%2 and %1/%3 are both %4")
                                        .arg(shard).arg(count).arg(other)
                                        .arg(name)));
                }
            }
        }
    }

    // runSuite() refuses to run with a value that is not a shard, and
    // the name is then the base name, as for no value
    void an_invalid_shard_is_the_base_name() {
        for (QString value : { "x", "2/2", "-1/2", "0/0", "1/2/3", "/" }) {
            QString name = shardApplicationName("test-tony-app", value);
            QVERIFY2(name == "test-tony-app",
                     qPrintable(QString("\"%1\" gives %2")
                                .arg(value).arg(name)));
        }
    }

    // Both parts must be whole numbers. Otherwise a slip such as "1x/2"
    // would be read as shard 0, and run it a second time under its name
    void a_shard_is_two_whole_numbers() {
        int shard = -1, count = -1;
        QVERIFY(parseShard("3/8", shard, count));
        QCOMPARE(shard, 3);
        QCOMPARE(count, 8);
        for (QString value : { "a/2", "/2", "1x/2", "1/x", "1/2x", "1.0/2" }) {
            QVERIFY2(!parseShard(value, shard, count),
                     qPrintable(QString("\"%1\" taken as %2/%3")
                                .arg(value).arg(shard).arg(count)));
            QCOMPARE(shardApplicationName("test-tony-app", value),
                     QString("test-tony-app"));
        }
    }

private:
    static QString shardName(int shard, int count) {
        return shardApplicationName
            ("test-tony-app", QString("%1/%2").arg(shard).arg(count));
    }
};

#endif
