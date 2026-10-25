#include <QApplication>
#include <QTemporaryDir>
#include <QtTest>
#include "playlist_store.h"

class player_tests : public QObject
{
    Q_OBJECT

   private slots:
    void remove_rows();
};

void player_tests::remove_rows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QStringList paths = {"a", "b", "c", "d", "e"};
    for (int current = -1; current < paths.size(); ++current)
    {
        for (int mask = 0; mask < (1 << paths.size()); ++mask)
        {
            QSettings settings(directory.filePath("playlist.ini"), QSettings::IniFormat);
            settings.setValue("playlists/order", QStringList{"test"});
            settings.setValue("playlists/test/paths", paths);
            settings.setValue("playlists/test/current_row", current);
            playlist_store store;
            store.load(settings);

            QList<int> rows = {-1, 99};
            QStringList remaining;
            QString expected_current;
            for (int row = 0; row < paths.size(); ++row)
            {
                if ((mask & (1 << row)) != 0)
                {
                    rows.prepend(row);
                    rows.prepend(row);
                    continue;
                }
                remaining.append(paths[row]);
                if (expected_current.isEmpty() && row >= current)
                {
                    expected_current = paths[row];
                }
            }
            if (expected_current.isEmpty() && !remaining.isEmpty())
            {
                expected_current = remaining.back();
            }

            QCOMPARE(store.remove_rows("test", rows), mask != 0);
            const auto *entry = store.playlist_by_id("test");
            QCOMPARE(entry->paths, remaining);
            if (mask == 0)
            {
                QCOMPARE(entry->current_row, current);
            }
            else if (remaining.isEmpty())
            {
                QCOMPARE(entry->current_row, -1);
            }
            else
            {
                QCOMPARE(entry->paths.value(entry->current_row), expected_current);
            }
        }
    }
}

QTEST_MAIN(player_tests)
#include "player_tests.moc"
