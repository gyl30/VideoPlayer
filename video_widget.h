#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <memory>
#include <utility>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMatrix4x4>
#include "media_objects.h"

extern "C"
{
#include <libavutil/pixfmt.h>
#include "libavutil/pixdesc.h"
}

class video_widget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

   public:
    explicit video_widget(QWidget *parent = nullptr);
    ~video_widget() override;

   public:
    void clear();

   public slots:
    void on_frame_ready(std::shared_ptr<media_frame> frame);

   protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

   private:
    void update_color_matrix(const AVFrame *frame);
    static QMatrix4x4 get_color_matrix(AVColorSpace space, AVColorRange range);

   private:
    int tex_width_ = 0;
    int tex_height_ = 0;
    GLuint textures_[3] = {0, 0, 0};
    bool texture_inited_ = false;
    QOpenGLShaderProgram *program_ = nullptr;
    std::shared_ptr<media_frame> current_frame_ = nullptr;

    AVColorSpace current_color_space_ = AVCOL_SPC_UNSPECIFIED;
    AVColorRange current_color_range_ = AVCOL_RANGE_UNSPECIFIED;
    QMatrix4x4 color_matrix_;
    int matrix_uniform_loc_ = -1;
};

#endif
