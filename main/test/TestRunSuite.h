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
// test.

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
};

#endif
