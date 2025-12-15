#include "video_widget.h"
#include <QOpenGLTexture>
#include <cstddef>
#include <cstring>

extern "C"
{
#include <libavutil/hwcontext.h>
}

static const char *kVertexShaderSource = R"(
    attribute highp vec4 vertex_in;
    attribute highp vec2 texture_in;
    varying highp vec2 texture_out;
    void main(void) {
        gl_Position = vertex_in;
        texture_out = texture_in;
    }
)";

static const char *kFragmentShaderSource = R"(
    varying highp vec2 texture_out;
    uniform sampler2D tex_y;
    uniform sampler2D tex_u;
    uniform sampler2D tex_v;
    uniform int format; 
    
    void main(void) {
        highp vec3 yuv;
        highp vec3 rgb;
        
        yuv.x = texture2D(tex_y, texture_out).r;
        
        if (format == 0) { 
            yuv.y = texture2D(tex_u, texture_out).r - 0.5;
            yuv.z = texture2D(tex_v, texture_out).r - 0.5;
        } else if (format == 1) { 
            highp vec2 uv = texture2D(tex_u, texture_out).rg;
            yuv.y = uv.r - 0.5;
            yuv.z = uv.g - 0.5;
        }
        
        rgb = mat3(1.0, 1.0, 1.0,
                   0.0, -0.344136, 1.772,
                   1.402, -0.714136, 0.0) * yuv;
        gl_FragColor = vec4(rgb, 1.0);
    }
)";

video_widget::video_widget(QWidget *parent) : QOpenGLWidget(parent), vbo_(QOpenGLBuffer::VertexBuffer) {}

video_widget::~video_widget()
{
    makeCurrent();
    vbo_.destroy();
    for (auto &i : pbo_)
    {
        for (auto &p : i)
        {
            p.destroy();
        }
    }
    for (auto &t : tex_)
    {
        t.reset();
    }
    program_.reset();
    doneCurrent();
    if (sw_frame_ != nullptr)
    {
        av_frame_free(&sw_frame_);
    }
}

void video_widget::update_frame(AVFrame *frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);

    if (frame->format == AV_PIX_FMT_CUDA || frame->format == AV_PIX_FMT_VAAPI || frame->format == AV_PIX_FMT_D3D11)
    {
        if (sw_frame_ == nullptr)
        {
            sw_frame_ = av_frame_alloc();
        }

        if (av_hwframe_transfer_data(sw_frame_, frame, 0) >= 0)
        {
            sw_frame_->pts = frame->pts;
            current_frame_ = sw_frame_;
        }
        else
        {
            return;
        }
    }
    else
    {
        current_frame_ = frame;
    }

    update();
}

void video_widget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    init_shader();
    static const float kVertices[] = {-1.0F, -1.0F, 0.0F, 1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
    vbo_.create();
    vbo_.bind();
    vbo_.allocate(kVertices, sizeof(kVertices));
    for (auto &i : pbo_)
    {
        for (auto &j : i)
        {
            j = QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer);
            j.create();
        }
    }
}

void video_widget::paintGL()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    glClear(GL_COLOR_BUFFER_BIT);

    if (current_frame_ == nullptr || !program_ || current_frame_->width == 0)
    {
        return;
    }

    int fmt_type = 0;
    if (current_frame_->format == AV_PIX_FMT_NV12)
    {
        fmt_type = 1;
    }
    else if (current_frame_->format != AV_PIX_FMT_YUV420P && current_frame_->format != AV_PIX_FMT_YUVJ420P)
    {
        return;
    }

    if (!texture_alloced_ || tex_width_ != current_frame_->width || tex_height_ != current_frame_->height || tex_format_ != fmt_type)
    {
        init_textures(current_frame_->width, current_frame_->height, fmt_type);
    }

    if (!texture_alloced_)
    {
        return;
    }

    upload_texture(current_frame_);

    program_->bind();
    vbo_.bind();
    program_->enableAttributeArray("vertex_in");
    program_->setAttributeBuffer("vertex_in", GL_FLOAT, 0, 2, 4 * sizeof(float));
    program_->enableAttributeArray("texture_in");
    program_->setAttributeBuffer("texture_in", GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    tex_[0]->bind(0);
    tex_[1]->bind(1);
    if (fmt_type == 0)
    {
        tex_[2]->bind(2);
    }

    program_->setUniformValue("tex_y", 0);
    program_->setUniformValue("tex_u", 1);
    program_->setUniformValue("tex_v", 2);
    program_->setUniformValue("format", fmt_type);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    tex_[0]->release();
    tex_[1]->release();
    if (fmt_type == 0)
    {
        tex_[2]->release();
    }
    program_->release();
}

void video_widget::upload_texture(AVFrame *frame)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int num_planes = (tex_format_ == 1) ? 2 : 3;
    int widths[3] = {frame->width, frame->width / 2, frame->width / 2};
    int heights[3] = {frame->height, frame->height / 2, frame->height / 2};

    if (tex_format_ == 1)
    {
        widths[1] = frame->width;
        heights[1] = frame->height / 2;
    }

    for (int i = 0; i < num_planes; i++)
    {
        int size = heights[i] * frame->linesize[i];
        if (tex_format_ == 1 && i == 1)
        {
            size = heights[i] * frame->linesize[1];
        }

        pbo_[pbo_index_][i].bind();
        if (pbo_[pbo_index_][i].size() < size)
        {
            pbo_[pbo_index_][i].allocate(size);
        }

        auto *ptr = static_cast<uint8_t *>(pbo_[pbo_index_][i].map(QOpenGLBuffer::WriteOnly));
        if (ptr != nullptr)
        {
            if (tex_format_ == 1 && i == 1)
            {
                std::memcpy(ptr, frame->data[1], static_cast<size_t>(size));
            }
            else
            {
                std::memcpy(ptr, frame->data[i], static_cast<size_t>(size));
            }
            pbo_[pbo_index_][i].unmap();
        }

        tex_[i]->bind();
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i]);
        if (tex_format_ == 1 && i == 1)
        {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 2);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, widths[i], heights[i], GL_RG, GL_UNSIGNED_BYTE, nullptr);
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, widths[i], heights[i], GL_RED, GL_UNSIGNED_BYTE, nullptr);
        }
        pbo_[pbo_index_][i].release();
    }
    pbo_index_ = (pbo_index_ + 1) % 2;
}

void video_widget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void video_widget::init_shader()
{
    program_ = std::make_unique<QOpenGLShaderProgram>();
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource);
    program_->link();
}

void video_widget::init_textures(int width, int height, int format)
{
    for (auto &t : tex_)
    {
        t.reset();
    }

    tex_width_ = width;
    tex_height_ = height;
    tex_format_ = format;

    auto create_tex = [&](int idx, int w, int h, QOpenGLTexture::TextureFormat internal_fmt, QOpenGLTexture::PixelFormat pixel_fmt)
    {
        tex_[idx] = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        tex_[idx]->setSize(w, h);
        tex_[idx]->setFormat(internal_fmt);
        tex_[idx]->allocateStorage(pixel_fmt, QOpenGLTexture::UInt8);
        tex_[idx]->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        tex_[idx]->setWrapMode(QOpenGLTexture::ClampToEdge);
    };

    if (format == 0)
    {
        create_tex(0, width, height, QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red);
        create_tex(1, width / 2, height / 2, QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red);
        create_tex(2, width / 2, height / 2, QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red);
    }
    else if (format == 1)
    {
        create_tex(0, width, height, QOpenGLTexture::R8_UNorm, QOpenGLTexture::Red);
        create_tex(1, width / 2, height / 2, QOpenGLTexture::RG8_UNorm, QOpenGLTexture::RG);
    }

    texture_alloced_ = true;
}
