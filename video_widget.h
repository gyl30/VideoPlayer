#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <memory>
#include "video_frame.h"

class video_widget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
   public:
    explicit video_widget(QWidget *parent = nullptr);
    ~video_widget() override;

   public slots:

    void update_frame(VideoFramePtr frame);

   protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

   private:
    void init_shader();
    void init_textures(int width, int height);

    VideoFramePtr current_frame_;
    bool texture_alloced_ = false;

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLTexture> tex_y_;
    std::unique_ptr<QOpenGLTexture> tex_u_;
    std::unique_ptr<QOpenGLTexture> tex_v_;
    QOpenGLBuffer vbo_;
};

#endif
