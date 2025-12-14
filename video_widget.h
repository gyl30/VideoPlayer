#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <memory>

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
    void update_frame(AVFrame* frame, int colorspace = 0);

   protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

   private:
    void init_shader();
    void init_textures(int width, int height);
    void upload_texture(AVFrame* frame);

    AVFrame* current_frame_ = nullptr;
    int current_colorspace_ = 0;
    bool texture_alloced_ = false;

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLTexture> tex_y_;
    std::unique_ptr<QOpenGLTexture> tex_u_;
    std::unique_ptr<QOpenGLTexture> tex_v_;
    QOpenGLBuffer vbo_;
    QOpenGLBuffer pbo_[3];
};

#endif
