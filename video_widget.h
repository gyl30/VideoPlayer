#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <memory>
#include <mutex>

extern "C"
{
#include <libavutil/frame.h>
}

class video_widget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
   public:
    explicit video_widget(QWidget* parent = nullptr);
    ~video_widget() override;

   public slots:
    void update_frame(AVFrame* frame);

   protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

   private:
    void init_shader();
    void init_textures(int width, int height, int format);
    void upload_texture(AVFrame* frame);

   private:
    AVFrame* current_frame_ = nullptr;
    AVFrame* sw_frame_ = nullptr;
    std::mutex frame_mutex_;

    bool texture_alloced_ = false;
    int tex_width_ = 0;
    int tex_height_ = 0;
    int tex_format_ = -1;

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLTexture> tex_[3];
    QOpenGLBuffer vbo_;

    QOpenGLBuffer pbo_[2][3];
    int pbo_index_ = 0;
};

#endif
