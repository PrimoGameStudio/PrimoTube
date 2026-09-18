#pragma once

#include <QPointer>
#include <QQuickFramebufferObject>

class MpvController;

// QML video surface for libmpv (OpenGL render API). Falls back gracefully:
// if the scene-graph isn't OpenGL or mpv is unavailable, `supported` is
// false and QML should show the QtMultimedia player instead.
class MpvVideoItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(MpvController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    MpvController *controller() const { return m_controller; }
    void setController(MpvController *c);
    bool supported() const { return m_supported; }

    Renderer *createRenderer() const override;

signals:
    void controllerChanged();
    void supportedChanged();
    void requestUpdate();

private slots:
    void onWindowChanged(QQuickWindow *win);

private:
    void updateSupported();
    MpvController *m_controller = nullptr;
    bool m_supported = false;
};
