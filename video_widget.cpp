#include "video_widget.h"
#include <QOpenGLTexture>
#include <QOpenGLPixelTransferOptions>

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
    uniform int colorspace;

    void main(void) {
        highp vec3 yuv;
        highp vec3 rgb;
        yuv.x = texture2D(tex_y, texture_out).r;
        yuv.y = texture2D(tex_u, texture_out).r - 0.5;
        yuv.z = texture2D(tex_v, texture_out).r - 0.5;
        
        if (colorspace == 1) {
            rgb = mat3(1.0, 1.0, 1.0,
                       0.0, -0.1873, 1.8556,
                       1.5748, -0.4681, 0.0) * yuv;
        } else {
            rgb = mat3(1.0, 1.0, 1.0,
                       0.0, -0.39465, 2.03211,
                       1.13983, -0.58060, 0.0) * yuv;
        }
        gl_FragColor = vec4(rgb, 1.0);
    }
)";

video_widget::video_widget(QWidget *parent) : QOpenGLWidget(parent), vbo_(QOpenGLBuffer::VertexBuffer) {}

video_widget::~video_widget()
{
    makeCurrent();
    vbo_.destroy();
    for (auto &p : pbo_) p.destroy();
    tex_y_.reset();
    tex_u_.reset();
    tex_v_.reset();
    program_.reset();
    doneCurrent();
}

void video_widget::update_frame(AVFrame *frame, int colorspace)
{
    current_frame_ = frame;
    current_colorspace_ = colorspace;
    update();
}

void video_widget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);

    init_shader();

    static const float kVertices[] = {
        -1.0F,
        -1.0F,
        0.0F,
        1.0F,
        1.0F,
        -1.0F,
        1.0F,
        1.0F,
        -1.0F,
        1.0F,
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        1.0F,
        0.0F,
    };

    vbo_.create();
    vbo_.bind();
    vbo_.allocate(kVertices, sizeof(kVertices));

    for (int i = 0; i < 3; i++)
    {
        pbo_[i].create();
    }
}

void video_widget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (!current_frame_ || !program_ || current_frame_->width == 0 || current_frame_->height == 0)
    {
        return;
    }

    int width = current_frame_->width;
    int height = current_frame_->height;

    if (!texture_alloced_ || tex_y_->width() != width || tex_y_->height() != height)
    {
        init_textures(width, height);
    }

    if (!texture_alloced_)
        return;

    upload_texture(current_frame_);

    program_->bind();
    vbo_.bind();

    program_->enableAttributeArray("vertex_in");
    program_->setAttributeBuffer("vertex_in", GL_FLOAT, 0, 2, 4 * sizeof(float));

    program_->enableAttributeArray("texture_in");
    program_->setAttributeBuffer("texture_in", GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    tex_y_->bind(0);
    tex_u_->bind(1);
    tex_v_->bind(2);

    program_->setUniformValue("tex_y", 0);
    program_->setUniformValue("tex_u", 1);
    program_->setUniformValue("tex_v", 2);
    program_->setUniformValue("colorspace", current_colorspace_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    tex_v_->release();
    tex_u_->release();
    tex_y_->release();
    vbo_.release();
    program_->release();
}

void video_widget::upload_texture(AVFrame *frame)
{
    int widths[3] = {frame->width, frame->width / 2, frame->width / 2};
    int heights[3] = {frame->height, frame->height / 2, frame->height / 2};
    QOpenGLTexture *textures[3] = {tex_y_.get(), tex_u_.get(), tex_v_.get()};

    for (int i = 0; i < 3; i++)
    {
        int size = widths[i] * heights[i];
        pbo_[i].bind();
        if (pbo_[i].size() != size)
        {
            pbo_[i].allocate(size);
        }

        uint8_t *ptr = (uint8_t *)pbo_[i].map(QOpenGLBuffer::WriteOnly);
        if (ptr)
        {
            uint8_t *src = frame->data[i];
            int linesize = frame->linesize[i];

            if (linesize == widths[i])
            {
                std::memcpy(ptr, src, size);
            }
            else
            {
                for (int h = 0; h < heights[i]; h++)
                {
                    std::memcpy(ptr + h * widths[i], src + h * linesize, widths[i]);
                }
            }
            pbo_[i].unmap();
        }

        textures[i]->bind();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[i].bufferId());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, widths[i], heights[i], GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

void video_widget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void video_widget::init_shader()
{
    program_ = std::make_unique<QOpenGLShaderProgram>();
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource);
    program_->link();
}

void video_widget::init_textures(int width, int height)
{
    tex_y_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    tex_u_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    tex_v_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);

    tex_y_->setSize(width, height);
    tex_y_->setFormat(QOpenGLTexture::R8_UNorm);
    tex_y_->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
    tex_y_->setMinificationFilter(QOpenGLTexture::Linear);
    tex_y_->setMagnificationFilter(QOpenGLTexture::Linear);
    tex_y_->setWrapMode(QOpenGLTexture::ClampToEdge);

    tex_u_->setSize(width / 2, height / 2);
    tex_u_->setFormat(QOpenGLTexture::R8_UNorm);
    tex_u_->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
    tex_u_->setMinificationFilter(QOpenGLTexture::Linear);
    tex_u_->setMagnificationFilter(QOpenGLTexture::Linear);
    tex_u_->setWrapMode(QOpenGLTexture::ClampToEdge);

    tex_v_->setSize(width / 2, height / 2);
    tex_v_->setFormat(QOpenGLTexture::R8_UNorm);
    tex_v_->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
    tex_v_->setMinificationFilter(QOpenGLTexture::Linear);
    tex_v_->setMagnificationFilter(QOpenGLTexture::Linear);
    tex_v_->setWrapMode(QOpenGLTexture::ClampToEdge);

    texture_alloced_ = true;
}
