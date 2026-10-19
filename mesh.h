#ifndef APOKOLIPS_MESH_H
#define APOKOLIPS_MESH_H

// -----------------------------------------------------------------
// PROOF OF CONCEPT — mesh warp warp
// -----------------------------------------------------------------
// This is a standalone test harness, not integrated into the shell.
// It opens a single OpenGL window, warps a PNG through a 20x20
// vertex mesh with a fragment shader, loops the animation, and
// prints the achieved fps to the terminal.
//
// If the fps is good, we integrate it as a fifth minimize style.
// If it's bad, we delete this file — nothing else in the codebase
// depends on it.
// -----------------------------------------------------------------

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <QVector2D>
#include <cmath>

class MeshTestWindow : public QOpenGLWidget,
                       protected QOpenGLFunctions {
public:
    explicit MeshTestWindow(const QString &pngPath,
                            QWidget *parent = nullptr)
        : QOpenGLWidget(parent), m_png(pngPath)
    {
        setFixedSize(900, 600);
        setWindowTitle("Mesh Warp — PoC");

        m_timer = new QTimer(this);
        m_timer->setInterval(16);          // ~60 Hz timer; measures
                                           // actual achieved fps
        QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
            m_t += 0.016;                  // 1.0 = one cycle
            if (m_t > 1.0) m_t -= 1.0;
            ++m_framesThisSecond;
            update();
        });
        m_timer->start();

        m_fps = new QTimer(this);
        m_fps->setInterval(1000);
        QObject::connect(m_fps, &QTimer::timeout, this, [this]() {
            qInfo() << "[mesh-poc] fps =" << m_framesThisSecond;
            m_framesThisSecond = 0;
        });
        m_fps->start();
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);

        m_prog = new QOpenGLShaderProgram(this);
        bool okV = m_prog->addShaderFromSourceCode(
            QOpenGLShader::Vertex,
            "attribute highp vec2 a_src;\n"
            "attribute highp vec2 a_uv;\n"
            "uniform highp vec2 u_windowSize;\n"
            "uniform highp vec2 u_dockPx;\n"
            "uniform highp vec2 u_tipSrc;\n"
            "uniform highp float u_diag;\n"
            "uniform highp float u_t;\n"
            "uniform highp float u_rateBase;\n"
            "varying highp vec2 v_uv;\n"
            "void main() {\n"
            "  highp float d = distance(a_src, u_tipSrc);\n"
            "  highp float dn = clamp(d / u_diag, 0.0, 1.0);\n"
            "  highp float rate = u_rateBase"
                              " - (u_rateBase - 0.5) * dn;\n"
            "  highp float progress ="
                              " 1.0 - pow(1.0 - u_t, rate);\n"
            "  highp vec2 dst ="
                              " a_src + (u_dockPx - a_src) * progress;\n"
            "  highp vec2 ndc ="
                              " dst / u_windowSize * 2.0 - 1.0;\n"
            "  ndc.y = -ndc.y;\n"
            "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "  v_uv = a_uv;\n"
            "}\n");
        bool okF = m_prog->addShaderFromSourceCode(
            QOpenGLShader::Fragment,
            "uniform sampler2D u_tex;\n"
            "varying highp vec2 v_uv;\n"
            "void main() {\n"
            "  gl_FragColor = texture2D(u_tex, v_uv);\n"
            "}\n");
        if (!okV) qWarning() << "[mesh-poc] vertex compile fail:"
                             << m_prog->log();
        if (!okF) qWarning() << "[mesh-poc] fragment compile fail:"
                             << m_prog->log();
        if (!m_prog->link()) {
            qWarning() << "[mesh-poc] link fail:" << m_prog->log();
        }

        QImage img(m_png);
        if (img.isNull()) {
            qWarning() << "[mesh-poc] cannot load PNG:" << m_png;
            return;
        }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        m_texW = img.width();
        m_texH = img.height();
        m_tex = new QOpenGLTexture(img);
        m_tex->setMinificationFilter(
            QOpenGLTexture::LinearMipMapLinear);
        m_tex->setMagnificationFilter(QOpenGLTexture::Linear);
        m_tex->setWrapMode(QOpenGLTexture::ClampToEdge);

        buildMesh();
        qInfo() << "[mesh-poc] initialized. texture=" << m_texW << "x"
                << m_texH << " verts=" << m_vertCount
                << " GL=" << (const char *)glGetString(GL_VERSION);
    }

    void buildMesh() {
        const int N = 20;   // 20x20 grid of vertices
        const float w = float(m_texW);
        const float h = float(m_texH);

        QVector<float> data;
        data.reserve((N - 1) * (N - 1) * 6 * 4);

        auto push = [&data](float x, float y, float u, float v) {
            data << x << y << u << v;
        };
        for (int j = 0; j < N - 1; ++j) {
            for (int i = 0; i < N - 1; ++i) {
                float x0 = w * i / (N - 1);
                float x1 = w * (i + 1) / (N - 1);
                float y0 = h * j / (N - 1);
                float y1 = h * (j + 1) / (N - 1);
                float u0 = float(i)     / (N - 1);
                float u1 = float(i + 1) / (N - 1);
                float v0 = float(j)     / (N - 1);
                float v1 = float(j + 1) / (N - 1);
                push(x0, y0, u0, v0);
                push(x1, y0, u1, v0);
                push(x1, y1, u1, v1);
                push(x0, y0, u0, v0);
                push(x1, y1, u1, v1);
                push(x0, y1, u0, v1);
            }
        }
        m_vertCount = data.size() / 4;

        m_vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        m_vbo->create();
        m_vbo->bind();
        m_vbo->allocate(data.constData(),
                        data.size() * int(sizeof(float)));
        m_vbo->release();
    }

    void resizeGL(int w, int h) override { glViewport(0, 0, w, h); }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT);
        if (!m_prog || !m_prog->isLinked() || !m_tex) return;

        m_prog->bind();
        m_tex->bind(0);
        m_prog->setUniformValue("u_tex", 0);
        m_prog->setUniformValue("u_windowSize",
                                QVector2D(float(width()),
                                          float(height())));
        m_prog->setUniformValue("u_dockPx",
                                QVector2D(float(width()) * 0.5f,
                                          float(height()) + 40.0f));
        // Tip corner — bottom-left of the texture, in window pixels.
        // The texture is scaled to fill the widget; window (0, h)
        // corresponds to texture bottom-left.
        m_prog->setUniformValue("u_tipSrc",
                                QVector2D(0.0f, float(m_texH)));
        m_prog->setUniformValue("u_diag",
                                float(std::hypot(m_texW, m_texH)));
        m_prog->setUniformValue("u_t", float(m_t));
        m_prog->setUniformValue("u_rateBase", 5.5f);

        m_vbo->bind();
        const int aSrc = m_prog->attributeLocation("a_src");
        const int aUv  = m_prog->attributeLocation("a_uv");
        m_prog->enableAttributeArray(aSrc);
        m_prog->enableAttributeArray(aUv);
        m_prog->setAttributeBuffer(aSrc, GL_FLOAT, 0, 2,
                                   4 * int(sizeof(float)));
        m_prog->setAttributeBuffer(aUv, GL_FLOAT,
                                   2 * int(sizeof(float)), 2,
                                   4 * int(sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, m_vertCount);
        m_prog->disableAttributeArray(aSrc);
        m_prog->disableAttributeArray(aUv);
        m_vbo->release();
        m_tex->release();
        m_prog->release();
    }

private:
    QString m_png;
    int     m_texW = 0, m_texH = 0;
    int     m_vertCount = 0;
    float   m_t = 0.0f;
    int     m_framesThisSecond = 0;
    QOpenGLShaderProgram *m_prog = nullptr;
    QOpenGLTexture       *m_tex  = nullptr;
    QOpenGLBuffer        *m_vbo  = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_fps   = nullptr;
};

#endif // APOKOLIPS_MESH_H
