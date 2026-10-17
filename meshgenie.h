#ifndef APOKOLIPS_MESHGENIE_H
#define APOKOLIPS_MESHGENIE_H

// -----------------------------------------------------------------
// GenieMeshOverlay — the mesh-warp genie minimize animation.
// Same visual shape as GenieOverlay, rendered via OpenGL fragment
// shader instead of 24 CPU quadToQuad transforms.
//
// Softened rate range (variant B): 3.5 → 0.65. Tip corner still
// leads, far side still trails, but the neck never pinches.
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
#include <QPixmap>
#include <QFile>
#include <QScreen>
#include <QApplication>
#include <cmath>

class GenieMeshOverlay : public QOpenGLWidget,
                        protected QOpenGLFunctions {
public:
    GenieMeshOverlay(const QString &pngPath, QRect startRect,
                     QPoint dockGlobal, bool reverse = false,
                     int durationOverrideMs = 0,
                     const QString &readySock = QString(),
                     QWidget *parent = nullptr)
        : QOpenGLWidget(parent), m_reverse(reverse)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        if (!m_pix.load(pngPath) || startRect.isEmpty()) {
            qWarning() << "[meshgenie] load failed:" << pngPath;
            QTimer::singleShot(0, qApp, &QApplication::quit);
            return;
        }
        QFile::remove(pngPath);

        QScreen *screen = QApplication::screenAt(startRect.center());
        if (!screen) screen = QApplication::primaryScreen();
        const QRect screenRect =
            screen ? screen->geometry() : QRect(0, 0, 1, 1);

        setGeometry(screenRect);
        m_origin = screenRect.topLeft();
        m_start  = startRect.translated(-m_origin);
        m_dock   = dockGlobal - m_origin;

        const auto &cfg = VisualConfigManager::instance().cfg();
        const int requestedMs = durationOverrideMs > 0
            ? durationOverrideMs : cfg.genieDurationMs;
        m_durationMs = qBound(128, requestedMs, 5000);

        qDebug() << "[meshgenie] reverse=" << m_reverse
                 << "durationMs=" << m_durationMs;

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, [this]() { tick(); });
        m_readySock = readySock;

        QTimer::singleShot(30, this, [this]() {
            if (m_timer) m_timer->start();
        });
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        if (m_pix.isNull() || m_start.isEmpty()) return;

        // ---- Shader program ----
        m_prog = new QOpenGLShaderProgram(this);
        const char *vertSrc =
            "attribute highp vec2 a_src;\n"
            "attribute highp vec2 a_uv;\n"
            "uniform highp vec2 u_windowSize;\n"
            "uniform highp vec2 u_dockPx;\n"
            "uniform highp vec2 u_tipSrc;\n"
            "uniform highp float u_diag;\n"
            "uniform highp float u_t;\n"
            "uniform highp float u_rateBase;\n"
            "uniform highp float u_rateMin;\n"
            "varying highp vec2 v_uv;\n"
            "void main() {\n"
            "  highp float d = distance(a_src, u_tipSrc);\n"
            "  highp float dn = clamp(d / u_diag, 0.0, 1.0);\n"
            // Geyser curve — exponential falloff in rate by distance
            // from the tip. Tip (dn=0) moves VERY fast; far side
            // (dn=1) barely budges until the very end, then snaps
            // in. This is what makes the tight neck + wide top.
            "  highp float rate = 8.0 * exp(-5.0 * dn) + 0.3;\n"
            "  highp float base = u_t * (2.0 - u_t);\n"
            "  highp float progress = 1.0 - pow(1.0 - base, rate);\n"
            "  highp vec2 dst ="
                              " a_src + (u_dockPx - a_src) * progress;\n"
            "  highp vec2 ndc ="
                              " dst / u_windowSize * 2.0 - 1.0;\n"
            "  ndc.y = -ndc.y;\n"
            "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "  v_uv = a_uv;\n"
            "}\n";
        const char *fragSrc =
            "uniform sampler2D u_tex;\n"
            "varying highp vec2 v_uv;\n"
            "void main() {\n"
            "  gl_FragColor = texture2D(u_tex, v_uv);\n"
            "}\n";

        if (!m_prog->addShaderFromSourceCode(
                QOpenGLShader::Vertex, vertSrc)) {
            qWarning() << "[meshgenie] vertex compile:"
                       << m_prog->log();
        }
        if (!m_prog->addShaderFromSourceCode(
                QOpenGLShader::Fragment, fragSrc)) {
            qWarning() << "[meshgenie] fragment compile:"
                       << m_prog->log();
        }
        if (!m_prog->link()) {
            qWarning() << "[meshgenie] link:" << m_prog->log();
        }

        // ---- Texture from the pixmap ----
        QImage img = m_pix.toImage().convertToFormat(
            QImage::Format_RGBA8888);
        m_tex = new QOpenGLTexture(img);
        m_tex->setMinificationFilter(QOpenGLTexture::Linear);
        m_tex->setMagnificationFilter(QOpenGLTexture::Linear);
        m_tex->setWrapMode(QOpenGLTexture::ClampToEdge);

        // ---- Mesh ----
        // Compute the tip corner (nearest of 4 to the dock) exactly
        // as GenieOverlay does, so both overlays pick the same tip.
        const auto dist = [](const QPointF &a, const QPointF &b) {
            return std::hypot(a.x() - b.x(), a.y() - b.y());
        };
        const QPointF dock(m_dock);
        const QPointF corners[4] = {
            QPointF(m_start.topLeft()), QPointF(m_start.topRight()),
            QPointF(m_start.bottomRight()), QPointF(m_start.bottomLeft())
        };
        qreal best = 1e18;
        for (int i = 0; i < 4; ++i) {
            qreal d = dist(corners[i], dock);
            if (d < best) { best = d; m_tipSrc = corners[i]; }
        }
        m_diag = qMax<qreal>(1.0, dist(corners[0], corners[2]));

        buildMesh();
        qInfo() << "[meshgenie] initialized. mesh=" << MESH_N
                << "x" << MESH_N
                << " verts=" << m_vertCount;
    }

    void resizeGL(int w, int h) override { glViewport(0, 0, w, h); }

    void paintGL() override {
        // Handshake ping on first paint — same protocol as
        // GenieOverlay. The shell waits for this before hiding
        // the real window.
        if (!m_readyPinged && !m_readySock.isEmpty()) {
            m_readyPinged = true;
            QTimer::singleShot(25, this, [this]() {
                genieHandshakeReady(m_readySock);
            });
        }

        glClear(GL_COLOR_BUFFER_BIT);
        if (!m_prog || !m_prog->isLinked() || !m_tex) return;

        const qreal t = m_reverse
            ? qBound<qreal>(0.0, 1.0 - m_t, 1.0)
            : qBound<qreal>(0.0, m_t, 1.0);

        m_prog->bind();
        m_tex->bind(0);
        m_prog->setUniformValue("u_tex", 0);
        m_prog->setUniformValue("u_windowSize",
            QVector2D(float(width()), float(height())));
        m_prog->setUniformValue("u_dockPx",
            QVector2D(float(m_dock.x()), float(m_dock.y())));
        m_prog->setUniformValue("u_tipSrc",
            QVector2D(float(m_tipSrc.x()), float(m_tipSrc.y())));
        m_prog->setUniformValue("u_diag", float(m_diag));
        m_prog->setUniformValue("u_t", float(t));
        // Variant B — softer rate range than the strip version's
        // 5.5 / 0.5. Tip still leads, but no hard neck.
        m_prog->setUniformValue("u_rateBase", 3.5f);
        m_prog->setUniformValue("u_rateMin",  0.40f);

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

        // Fire "done" AFTER the final frame has actually painted —
        // not from the timer, which on slow hardware runs ahead of
        // the visual. This is the fix for the "window reveals before
        // the animation finishes" bug on both directions.
        if (m_t >= 1.0 && !m_doneFired) {
            m_doneFired = true;
            if (m_timer) m_timer->stop();
            QTimer::singleShot(30, this, [this]() {
                if (!m_readySock.isEmpty())
                    genieHandshakeDone(m_readySock);
                QTimer::singleShot(80, this, [this]() { close(); });
            });
        }
    }

private:
    static constexpr int MESH_N = 48;   // 48x48 vertex grid

    void buildMesh() {
        const qreal x0 = m_start.left();
        const qreal y0 = m_start.top();
        const qreal w  = m_start.width();
        const qreal h  = m_start.height();

        QVector<float> data;
        data.reserve((MESH_N - 1) * (MESH_N - 1) * 6 * 4);

        auto push = [&data](float x, float y, float u, float v) {
            data << x << y << u << v;
        };
        for (int j = 0; j < MESH_N - 1; ++j) {
            for (int i = 0; i < MESH_N - 1; ++i) {
                float u0 = float(i)     / (MESH_N - 1);
                float u1 = float(i + 1) / (MESH_N - 1);
                float v0 = float(j)     / (MESH_N - 1);
                float v1 = float(j + 1) / (MESH_N - 1);
                float xA = float(x0 + u0 * w);
                float xB = float(x0 + u1 * w);
                float yA = float(y0 + v0 * h);
                float yB = float(y0 + v1 * h);
                // Two triangles per cell.
                push(xA, yA, u0, v0);
                push(xB, yA, u1, v0);
                push(xB, yB, u1, v1);
                push(xA, yA, u0, v0);
                push(xB, yB, u1, v1);
                push(xA, yB, u0, v1);
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

    void tick() {
        if (!m_elapsed.isValid()) m_elapsed.start();
        const qint64 ms = m_elapsed.elapsed();
        if (ms >= m_durationMs) {
            m_t = 1.0;   // paintGL will fire "done" after painting
        } else {
            m_t = double(ms) / double(m_durationMs);
        }
        update();
    }

    QPixmap m_pix;
    QRect   m_start;
    QPoint  m_dock;
    QPoint  m_origin;
    QPointF m_tipSrc;
    qreal   m_diag = 1.0;

    QTimer        *m_timer = nullptr;
    QElapsedTimer  m_elapsed;
    qreal          m_t = 0.0;
    int            m_durationMs = 1140;
    bool           m_reverse = false;
    bool           m_doneFired = false;

    QString m_readySock;
    bool    m_readyPinged = false;
    int     m_vertCount = 0;

    QOpenGLShaderProgram *m_prog = nullptr;
    QOpenGLTexture       *m_tex  = nullptr;
    QOpenGLBuffer        *m_vbo  = nullptr;
};

#endif // APOKOLIPS_MESHGENIE_H
