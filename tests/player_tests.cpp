#include <QApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QtTest>
#include "playlist_store.h"
#include "main_window.h"
#include "log.h"

#ifdef TEST_FFMPEG_FAILURES
namespace
{
std::atomic<bool> fail_stream_info{false};
std::atomic<int> fail_codec_type{-1};
std::atomic<int> closed_inputs{0};
std::atomic<int> seek_calls{0};
}

extern "C"
{
int __real_avformat_find_stream_info(AVFormatContext *, AVDictionary **);
void __real_avformat_close_input(AVFormatContext **);
int __real_avcodec_open2(AVCodecContext *, const AVCodec *, AVDictionary **);
int __real_avformat_seek_file(AVFormatContext *, int, int64_t, int64_t, int64_t, int);

int __wrap_avformat_find_stream_info(AVFormatContext *context, AVDictionary **options)
{
    if (fail_stream_info.load())
    {
        return AVERROR(EIO);
    }
    return __real_avformat_find_stream_info(context, options);
}

void __wrap_avformat_close_input(AVFormatContext **context)
{
    if (*context != nullptr)
    {
        ++closed_inputs;
    }
    __real_avformat_close_input(context);
}

int __wrap_avcodec_open2(AVCodecContext *context, const AVCodec *codec, AVDictionary **options)
{
    if (context->codec_type == fail_codec_type.load())
    {
        return AVERROR(EINVAL);
    }
    return __real_avcodec_open2(context, codec, options);
}

int __wrap_avformat_seek_file(AVFormatContext *context, int stream, int64_t min, int64_t target, int64_t max, int flags)
{
    const int result = __real_avformat_seek_file(context, stream, min, target, max, flags);
    ++seek_calls;
    return result;
}
}
#endif

class player_tests : public QObject
{
    Q_OBJECT

   private slots:
    void initTestCase();
    void init();
    void control_layout();
    void remove_rows();
    void sync_stop_data();
    void sync_stop();
    void stop_before_decode();
#ifdef TEST_FFMPEG_FAILURES
    void init_failure_data();
    void init_failure();
    void stale_seek_data();
    void stale_seek();
#endif

   private:
    void set_playlist(const QStringList &paths);
    QTemporaryDir directory_;
    QString audio_path_;
    QString other_audio_path_;
    QString video_path_;
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
    set_level("warn");

    audio_path_ = directory_.filePath("audio.wav");
    video_path_ = directory_.filePath("video.mkv");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=4",
                            "-y", audio_path_});
    QVERIFY(ffmpeg.waitForFinished());
    QCOMPARE(ffmpeg.exitCode(), 0);
    other_audio_path_ = directory_.filePath("other.wav");
    QVERIFY(QFile::copy(audio_path_, other_audio_path_));
    ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10:duration=1",
                            "-f", "lavfi", "-i", "sine=duration=1", "-c:v", "mpeg4", "-c:a", "pcm_s16le",
                            "-y", video_path_});
    QVERIFY(ffmpeg.waitForFinished());
    QCOMPARE(ffmpeg.exitCode(), 0);
}

void player_tests::init()
{
    QSettings settings("gyl30", "VideoPlayer");
    settings.clear();
#ifdef TEST_FFMPEG_FAILURES
    fail_stream_info.store(false);
    fail_codec_type.store(-1);
    closed_inputs.store(0);
    seek_calls.store(0);
#endif
}

void player_tests::set_playlist(const QStringList &paths)
{
    playlist_store store;
    store.ensure_default_playlist();
    for (const QString &path : paths)
    {
        store.add_path(store.active_playlist_id(), path);
    }
    QSettings settings("gyl30", "VideoPlayer");
    store.save(settings);
}

void player_tests::stop_before_decode()
{
    safe_queue<std::shared_ptr<media_packet>> packets;
    safe_queue<std::shared_ptr<media_frame>> frames;
    demuxer source;
    QVERIFY(source.open(audio_path_.toStdString(), nullptr, &packets));
    decoder audio;
    QVERIFY(audio.open(source.codec_par(source.audio_index()), &packets, &frames, "Test"));
    audio.stop();
    packets.abort();
    frames.abort();
    std::atomic<bool> finished{false};
    std::thread worker([&]() { audio.run(); finished.store(true); });
    QTest::qSleep(100);
    const bool stopped = finished.load();
    audio.stop();
    worker.join();
    QVERIFY(stopped);
}

#ifdef TEST_FFMPEG_FAILURES
void player_tests::stale_seek_data()
{
    QTest::addColumn<QByteArray>("action");
    QTest::newRow("切换媒体") << QByteArray("on_play_next");
    QTest::newRow("停止媒体") << QByteArray("on_stop_pressed");
}

void player_tests::stale_seek()
{
    QFETCH(QByteArray, action);
    set_playlist({audio_path_, other_audio_path_});
    main_window window;
    QVERIFY(QMetaObject::invokeMethod(&window, "on_toggle_pause", Qt::DirectConnection));
    auto *slider = window.findChild<QSlider *>("seekSlider");
    QVERIFY(slider->isEnabled());
    slider->setValue(2);
    QVERIFY(QMetaObject::invokeMethod(&window, "on_slider_released", Qt::DirectConnection));
    QElapsedTimer deadline;
    deadline.start();
    while (seek_calls.load() == 0 && deadline.elapsed() < 1000)
    {
        QTest::qSleep(1);
    }
    QVERIFY(seek_calls.load() > 0);
    QVERIFY(QMetaObject::invokeMethod(&window, action.constData(), Qt::DirectConnection));
    QCOMPARE(slider->value(), 0);
    QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);
    QCOMPARE(slider->value(), 0);
    QCOMPARE(window.findChild<QLabel *>("timeLabel")->text().left(8), QString("00:00:00"));
}

void player_tests::init_failure_data()
{
    QTest::addColumn<int>("stage");
    QTest::newRow("读取流信息") << 0;
    QTest::newRow("视频解码器") << 1;
    QTest::newRow("音频解码器") << 2;
    QTest::newRow("音频设备") << 3;
}

void player_tests::init_failure()
{
    QFETCH(int, stage);
    set_playlist({video_path_, audio_path_});
    main_window window;
    fail_stream_info.store(stage == 0);
    fail_codec_type.store(stage == 1 ? AVMEDIA_TYPE_VIDEO : stage == 2 ? AVMEDIA_TYPE_AUDIO : -1);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qputenv("SDL_AUDIODRIVER", stage == 3 ? "invalid-test-driver" : "dummy");
    QTimer::singleShot(0, []()
    {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *dialog = qobject_cast<QMessageBox *>(widget))
            {
                dialog->accept();
            }
        }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "on_toggle_pause", Qt::DirectConnection));
    fail_stream_info.store(false);
    fail_codec_type.store(-1);
    qputenv("SDL_AUDIODRIVER", "dummy");
    QCOMPARE(closed_inputs.load(), 1);
    QVERIFY(!window.findChild<QSlider *>("seekSlider")->isEnabled());
    QCOMPARE(window.findChild<QLabel *>("timeLabel")->text(), QString("00:00:00 / 00:00:00"));
    QVERIFY(QMetaObject::invokeMethod(&window, "on_play_next", Qt::DirectConnection));
    QVERIFY(window.findChild<QSlider *>("seekSlider")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&window, "on_stop_pressed", Qt::DirectConnection));
}
#endif

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
