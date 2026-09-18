#include "mpvitem.h"
#include "mpvcontroller.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {
void *getProcAddress(void * /*ctx*/, const char *name)
{
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    if (!gl)
        return nullptr;
    return reinterpret_cast<void *>(gl->getProcAddress(QByteArray(name)));
}

class MpvRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(MpvVideoItem *item)
        : m_item(item)
    {
    }

    ~MpvRenderer() override
    {
        if (m_item)
            QObject::disconnect(m_updateConn);
        if (m_ctx) {
            // Clear the C callback first so no in-flight mpv update can
            // touch the (possibly destroyed) item after this point.
            mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
            mpv_render_context_free(m_ctx);
        }
        delete static_cast<QPointer<MpvVideoItem> *>(m_callbackCtx);
        m_callbackCtx = nullptr;
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        if (!m_item || !m_item->controller() || !m_item->controller()->handle())
            return nullptr;
        mpv_handle *mpv = m_item->controller()->handle();
        if (!m_ctx) {
            mpv_opengl_init_params glParams{getProcAddress, nullptr};
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_create(&m_ctx, mpv, params) < 0) {
                m_ctx = nullptr;
                return nullptr;
            }
            mpv_render_context_set_update_callback(
                m_ctx,
                [](void *ctx) {
                    auto *weak = static_cast<QPointer<MpvVideoItem> *>(ctx);
                    if (weak && *weak)
                        emit(*weak)->requestUpdate();
                },
                (m_callbackCtx = new QPointer<MpvVideoItem>(m_item)));
            QPointer<MpvVideoItem> weakItem = m_item;
            m_updateConn = QObject::connect(
                m_item.data(), &MpvVideoItem::requestUpdate, m_item.data(),
                [weakItem]() {
                    if (weakItem)
                        weakItem->update();
                },
                Qt::QueuedConnection);
        }
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void render() override
    {
        if (!m_ctx)
            return;
        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!fbo)
            return;
        mpv_opengl_fbo glFbo{
            static_cast<int>(fbo->handle()),
            fbo->width(),
            fbo->height(),
            0,
        };
        // FBO target (sampled as a texture by Qt Quick): render unflipped.
        // FLIP_Y != 0 is only for the OpenGL *default* framebuffer, which
        // has a flipped coordinate system (see mpv/render.h).
        int flip = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &glFbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_ctx, params);
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        Q_UNUSED(item)
        // Pull latest size/state on the render thread if needed.
    }

private:
    QPointer<MpvVideoItem> m_item = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QMetaObject::Connection m_updateConn;
    void *m_callbackCtx = nullptr;
};
} // namespace

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    connect(this, &QQuickItem::windowChanged, this, &MpvVideoItem::onWindowChanged);
}

void MpvVideoItem::setController(MpvController *c)
{
    if (m_controller == c)
        return;
    m_controller = c;
    emit controllerChanged();
    updateSupported();
    update();
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    // Only OpenGL RHI backends can host libmpv's GL renderer. Unknown means
    // the window/RHI is not ready yet — do not claim support; onWindowChanged
    // will set it once the real graphics API is known. This keeps QML on the
    // QtMultimedia fallback instead of a black mpv surface on Vulkan/Metal.
    QQuickWindow *win = window();
    const bool gl = win && win->graphicsApi() == QSGRendererInterface::OpenGL;
    if (!gl || !m_controller || !m_controller->handle())
        return nullptr;
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}

void MpvVideoItem::onWindowChanged(QQuickWindow *win)
{
    Q_UNUSED(win)
    updateSupported();
}

void MpvVideoItem::updateSupported()
{
    QQuickWindow *win = window();
    const bool gl = win && win->graphicsApi() == QSGRendererInterface::OpenGL;
    // Require a usable controller/handle too, so QML never picks the mpv
    // path when libmpv failed to init (black surface otherwise).
    const bool ok = gl && m_controller && m_controller->handle();
    if (m_supported != ok) {
        m_supported = ok;
        emit supportedChanged();
    }
}
