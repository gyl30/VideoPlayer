#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class video_decoder : public QThread
{
    Q_OBJECT
   public:
    explicit video_decoder(QObject *parent = nullptr);
    ~video_decoder() override;

   public:
    bool open(const QString &file_path);
    void stop();

   signals:
    void frame_ready(QImage image);

   protected:
    void run() override;

   private:
    void clear();

   private:
    QString file_;
    std::atomic<bool> stop_;
};

#endif
