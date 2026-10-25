#include <QApplication>
#include <QTemporaryDir>
#include <QtTest>
#include "playlist_store.h"
#include "main_window.h"

class player_tests : public QObject
{
    Q_OBJECT

   private slots:
    void initTestCase();
    void control_layout();
    void remove_rows();
    void sync_stop_data();
    void sync_stop();

   private:
    QTemporaryDir directory_;
};

void player_tests::sync_stop_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("等待空队列") << 0;
    QTest::newRow("暂停") << 1;
    QTest::newRow("等待未来帧") << 2;
}

void player_tests::sync_stop()
{
    QFETCH(int, mode);
    safe_queue<std::shared_ptr<media_frame>> frames;
    safe_queue<std::shared_ptr<media_packet>> packets;
    av_clock clock;
    clock.set(0.0, packets.serial());
    video_sync_thread sync(&frames, &packets, AVRational{1, 1}, &clock);
    if (mode == 1)
    {
        sync.paused(true);
    }
    if (mode == 2)
    {
        auto frame = std::make_shared<media_frame>();
        frame->raw()->pts = 60;
        QVERIFY(frames.push(frame));
    }
    sync.start();
    QTest::qSleep(30);
    frames.abort();
    sync.stop();
    QVERIFY(sync.wait(1000));
}

void player_tests::initTestCase()
{
    QVERIFY(directory_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory_.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory_.path());
    QSettings settings("gyl30", "VideoPlayer");
    QVERIFY(settings.fileName().startsWith(directory_.path()));
}

void player_tests::control_layout()
{
    main_window window;
    auto *bar = window.findChild<QWidget *>("controlBar");
    QVERIFY(bar != nullptr);
    const auto rows = bar->findChildren<QHBoxLayout *>();
    QCOMPARE(rows.size(), 1);
    QStringList controls;
    for (int i = 0; i < rows.front()->count(); ++i)
    {
        QWidget *widget = rows.front()->itemAt(i)->widget();
        if (widget == nullptr)
        {
            continue;
        }
        controls.append(widget->toolTip().isEmpty() ? widget->objectName() : widget->toolTip());
    }
    QCOMPARE(controls, QStringList({"timeLabel", "播放速度：1x", "volumeIcon", "音量", "停止", "播放上一个",
                                   "播放/暂停", "播放下一个", "隐藏视频画面，仅播放音频",
                                   "开启后新打开的文件优先使用硬件解码，失败时回退软解", "打开媒体", "当前媒体没有视频画面", "显示播放列表"}));
    QCOMPARE(window.findChild<QWidget *>("controlPanel")->height(), 96);
    QCOMPARE(window.findChild<QLabel *>("timeLabel")->minimumWidth(), 168);
    for (const QSize &size : {QSize(960, 480), QSize(1600, 900)})
    {
        window.resize(size);
        QCOMPARE(rows.front()->count(), 17);
        QCOMPARE(window.findChild<QWidget *>("controlPanel")->height(), 96);
    }
}

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
