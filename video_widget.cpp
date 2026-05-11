#include <algorithm>
#include "log.h"
#include "video_widget.h"

extern "C"
{
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace
{
struct display_rect
{
    int width = 1;
    int height = 1;
};

display_rect calculate_display_rect(int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width = 0;
    int64_t height = 0;

    if (scr_width <= 0 || scr_height <= 0 || pic_width <= 0 || pic_height <= 0)
    {
        return {};
    }

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
    {
        aspect_ratio = av_make_q(1, 1);
    }

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1LL;
    if (width > scr_width)
    {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1LL;
    }

    return {std::max(1, static_cast<int>(width)), std::max(1, static_cast<int>(height))};
}
}  // namespace

video_widget::video_widget(QWidget *parent) : QOpenGLWidget(parent) { LOG_INFO("video widget constructed"); }

video_widget::~video_widget()
{
    LOG_INFO("video widget destroying");
    cleanup_gl_resources();
}

void video_widget::cleanup_gl_resources()
{
    if (context() == nullptr)
    {
        if (program_ != nullptr)
        {
            delete program_;
            program_ = nullptr;
        }
        program_ = nullptr;
        texture_inited_ = false;
        tex_width_ = 0;
        tex_height_ = 0;
        return;
    }

    makeCurrent();

    if (program_ != nullptr)
    {
        delete program_;
        program_ = nullptr;
    }

    if (texture_inited_)
    {
        glDeleteTextures(3, textures_);
        textures_[0] = 0;
        textures_[1] = 0;
        textures_[2] = 0;
        texture_inited_ = false;
    }

    tex_width_ = 0;
    tex_height_ = 0;
    doneCurrent();
}

void video_widget::clear()
{
    current_frame_ = nullptr;
    update();
}

void video_widget::on_frame_ready(std::shared_ptr<media_frame> frame)
{
    if (frame == nullptr)
    {
        return;
    }

    auto *raw = frame->raw();
    if (raw->colorspace != current_color_space_ || raw->color_range != current_color_range_)
    {
        update_color_matrix(raw);
    }

    current_frame_ = std::move(frame);
    update();
}

void video_widget::initializeGL()
{
    LOG_INFO("video widget initialize gl");
    initializeOpenGLFunctions();

    if (context() != nullptr)
    {
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &video_widget::cleanup_gl_resources, Qt::UniqueConnection);
    }

    if (program_ != nullptr || texture_inited_)
    {
        cleanup_gl_resources();
        makeCurrent();
    }

    program_ = new QOpenGLShaderProgram(this);
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                      "#version 120\n"
                                      "attribute vec4 position;\n"
                                      "attribute vec2 texCoord;\n"
                                      "varying vec2 vTexCoord;\n"
                                      "void main() {\n"
                                      "    gl_Position = position;\n"
                                      "    vTexCoord = texCoord;\n"
                                      "}\n");

    program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                      "#version 120\n"
                                      "varying vec2 vTexCoord;\n"
                                      "uniform sampler2D texY;\n"
                                      "uniform sampler2D texU;\n"
                                      "uniform sampler2D texV;\n"
                                      "uniform mat4 colorMatrix;\n"
                                      "void main() {\n"
                                      "    vec3 yuv;\n"
                                      "    yuv.x = texture2D(texY, vTexCoord).r;\n"
                                      "    yuv.y = texture2D(texU, vTexCoord).r;\n"
                                      "    yuv.z = texture2D(texV, vTexCoord).r;\n"
                                      "    gl_FragColor = colorMatrix * vec4(yuv, 1.0);\n"
                                      "}\n");

    if (!program_->link())
    {
        LOG_ERROR("video widget shader link failed");
    }
    else
    {
        LOG_INFO("video widget shader linked successfully");
    }

    matrix_uniform_loc_ = program_->uniformLocation("colorMatrix");
    glGenTextures(3, textures_);
    tex_width_ = 0;
    tex_height_ = 0;
    texture_inited_ = false;

    color_matrix_ = get_color_matrix(AVCOL_SPC_BT470BG, AVCOL_RANGE_MPEG);
}

void video_widget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void video_widget::paintGL()
{
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    if (current_frame_ == nullptr)
    {
        return;
    }

    if (!program_->bind())
    {
        LOG_ERROR("video widget failed to bind shader program");
        return;
    }

    auto *raw = current_frame_->raw();
    const int chroma_width = (raw->width + 1) / 2;
    const int chroma_height = (raw->height + 1) / 2;

    if (raw->width != tex_width_ || raw->height != tex_height_)
    {
        tex_width_ = raw->width;
        tex_height_ = raw->height;
        LOG_INFO("video widget texture resize to {}x{}", tex_width_, tex_height_);

        for (int i = 0; i < 3; i++)
        {
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            const int w = (i == 0) ? tex_width_ : chroma_width;
            const int h = (i == 0) ? tex_height_ : chroma_height;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        texture_inited_ = true;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, raw->linesize[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width_, tex_height_, GL_RED, GL_UNSIGNED_BYTE, raw->data[0]);
    program_->setUniformValue("texY", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, raw->linesize[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height, GL_RED, GL_UNSIGNED_BYTE, raw->data[1]);
    program_->setUniformValue("texU", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures_[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, raw->linesize[2]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height, GL_RED, GL_UNSIGNED_BYTE, raw->data[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    program_->setUniformValue("texV", 2);

    if (matrix_uniform_loc_ >= 0)
    {
        program_->setUniformValue(matrix_uniform_loc_, color_matrix_);
    }

    const int widget_width = std::max(width(), 1);
    const int widget_height = std::max(height(), 1);
    const display_rect rect = calculate_display_rect(widget_width, widget_height, tex_width_, tex_height_, raw->sample_aspect_ratio);

    const GLfloat x_scale = static_cast<GLfloat>(static_cast<double>(rect.width) / static_cast<double>(widget_width));
    const GLfloat y_scale = static_cast<GLfloat>(static_cast<double>(rect.height) / static_cast<double>(widget_height));

    const GLfloat vertices[] = {-x_scale, -y_scale, x_scale, -y_scale, -x_scale, y_scale, x_scale, y_scale};
    static const GLfloat texCoords[] = {0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F};

    const int posLoc = program_->attributeLocation("position");
    program_->enableAttributeArray(posLoc);
    program_->setAttributeArray(posLoc, vertices, 2);

    const int texLoc = program_->attributeLocation("texCoord");
    program_->enableAttributeArray(texLoc);
    program_->setAttributeArray(texLoc, texCoords, 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    program_->disableAttributeArray(posLoc);
    program_->disableAttributeArray(texLoc);
    program_->release();
}

void video_widget::update_color_matrix(const AVFrame *frame)
{
    AVColorSpace space = frame->colorspace;
    AVColorRange range = frame->color_range;
    const int width = frame->width;
    const int height = frame->height;

    if (space == AVCOL_SPC_UNSPECIFIED)
    {
        if (width >= 1280 || height >= 720)
        {
            space = AVCOL_SPC_BT709;
        }
        else
        {
            space = AVCOL_SPC_BT470BG;
        }
    }

    if (range == AVCOL_RANGE_UNSPECIFIED)
    {
        range = AVCOL_RANGE_MPEG;
    }

    if (space == current_color_space_ && range == current_color_range_)
    {
        return;
    }

    current_color_space_ = space;
    current_color_range_ = range;
    LOG_INFO("video widget updating color matrix space {} range {}", av_color_space_name(space), av_color_range_name(range));
    color_matrix_ = get_color_matrix(space, range);
}

QMatrix4x4 video_widget::get_color_matrix(AVColorSpace space, AVColorRange range)
{
    QMatrix4x4 mat;

    float kr = 0.299F;
    float kb = 0.114F;

    if (space == AVCOL_SPC_BT709)
    {
        kr = 0.2126F;
        kb = 0.0722F;
    }
    else if (space == AVCOL_SPC_BT2020_NCL || space == AVCOL_SPC_BT2020_CL)
    {
        kr = 0.2627F;
        kb = 0.0593F;
    }

    const float kg = 1.0F - kr - kb;

    float y_off = 0.0F;
    float uv_off = 0.5F;
    float y_scale = 1.0F;
    float uv_scale = 1.0F;

    if (range == AVCOL_RANGE_MPEG)
    {
        y_off = 16.0F / 255.0F;
        uv_off = 128.0F / 255.0F;
        y_scale = 255.0F / (235.0F - 16.0F);
        uv_scale = 255.0F / (240.0F - 16.0F);
    }

    const float r_v = 2.0F * (1.0F - kr);
    const float b_u = 2.0F * (1.0F - kb);
    const float g_u = -(b_u * kb) / kg;
    const float g_v = -(r_v * kr) / kg;

    const float r_y_coeff = y_scale;
    const float r_v_coeff = r_v * uv_scale;
    const float r_const = -(y_scale * y_off) - (r_v * uv_scale * uv_off);

    const float g_y_coeff = y_scale;
    const float g_u_coeff = g_u * uv_scale;
    const float g_v_coeff = g_v * uv_scale;
    const float g_const = -(y_scale * y_off) - (g_u * uv_scale * uv_off) - (g_v * uv_scale * uv_off);

    const float b_y_coeff = y_scale;
    const float b_u_coeff = b_u * uv_scale;
    const float b_const = -(y_scale * y_off) - (b_u * uv_scale * uv_off);

    mat.setRow(0, QVector4D(r_y_coeff, 0.0F, r_v_coeff, r_const));
    mat.setRow(1, QVector4D(g_y_coeff, g_u_coeff, g_v_coeff, g_const));
    mat.setRow(2, QVector4D(b_y_coeff, b_u_coeff, 0.0F, b_const));
    mat.setRow(3, QVector4D(0.0F, 0.0F, 0.0F, 1.0F));

    return mat;
}
