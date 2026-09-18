#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class MpvController;
class QWindow;

// Minimal MPRIS (org.mpris.MediaPlayer2) bridge for PrimoTube.
//
// Exposes the current video + transport state on the session bus as
// `org.mpris.MediaPlayer2.primotube` so desktop widgets (playerctl,
// KDE/GNOME shells) can show metadata and drive Play/Pause/Stop/Seek.
//
// Design notes:
// - "Minimal playback only": no track list, Next/Previous are advertised
//   as unsupported (CanGoNext/CanGoPrevious=false) and are no-ops.
// - Both playback paths are covered: QML pushes the *effective* state
//   (libmpv or the QtMultimedia fallback) via setCurrentTrack() /
//   setEffectiveState(). D-Bus actions drive libmpv directly when it owns
//   playback, otherwise they emit request*() signals that PlayerPage.qml
//   handles for the fallback player.
// - Headless-safe: registerService() returns false (no crash) when there
//   is no session bus, and all state helpers work without a bus so unit
//   tests can run in CI. When Qt DBus is unavailable at build time the
//   same API compiles as a no-op stub (PRIMOTUBE_HAS_MPRIS unset).
class MprisAdapter : public QObject {
    Q_OBJECT
public:
    static const char *serviceName() { return "org.mpris.MediaPlayer2.primotube"; }
    static const char *objectPath() { return "/org/mpris/MediaPlayer2"; }
    static const char *noTrackId() { return "/org/mpris/MediaPlayer2/TrackList/NoTrack"; }

    explicit MprisAdapter(MpvController *mpvController, QObject *parent = nullptr);

    // Registers the service + object on the session bus. Returns false
    // when there is no session bus or registration fails (never fatal).
    bool registerService();
    bool isRegistered() const { return m_registered; }

    void setRaiseWindow(QWindow *window) { m_raiseWindow = window; }

    // --- QML push API (effective state, covers mpv + compat paths) ---
    Q_INVOKABLE void setCurrentTrack(const QString &videoId, const QString &title,
                                     const QString &channel, const QString &watchUrl,
                                     const QString &thumbnail, double durationSec,
                                     const QString &description = QString());
    Q_INVOKABLE void setEffectiveState(bool playing, double positionSec, double durationSec,
                                       double volume, bool mpvEffective);
    Q_INVOKABLE void clearTrack();

    // --- Exposed state (used by the D-Bus adaptors and unit tests) ---
    bool hasTrack() const { return m_hasTrack; }
    QString playbackStatus() const; // "Playing", "Paused" or "Stopped"
    QVariantMap metadata() const;
    qlonglong positionMicros() const { return secondsToMicros(m_positionSec); }
    qlonglong durationMicros() const { return secondsToMicros(m_durationSec); }
    double volume() const { return m_volume; }
    QString currentTrackId() const;

    // Pure helpers (headless-testable, no bus required).
    static QString statusForState(bool hasTrack, bool playing);
    static qlonglong secondsToMicros(double seconds);
    static double microsToSeconds(qlonglong micros);
    static double clampVolume(double v);
    static double clampPosition(double pos, double duration);
    static QVariantMap buildMetadata(const QString &trackObjectPath, const QString &title,
                                     const QString &channel, const QString &watchUrl,
                                     const QString &thumbnail, qlonglong lengthMicros,
                                     const QString &description);

signals:
    // D-Bus -> fallback-player requests. QML handles these only when the
    // QtMultimedia fallback owns playback (!effectiveMpv); the mpv path is
    // driven directly in C++ so these are not emitted for it.
    void requestPlay();
    void requestPause();
    void requestToggle();
    void requestStop();
    void requestSeek(double positionSec);
    void requestVolume(double volume);
    void requestOpenUri(const QString &url);

    // Internal: emitted when exposed state changes so the D-Bus layer can
    // relay PropertiesChanged / Seeked. Position updates alone do not emit
    // (MPRIS clients poll Position; only jumps emit Seeked).
    void playbackStatusChanged();
    void metadataChanged();
    void volumeChanged();
    void seeked(qlonglong positionMicros);

public: // Entry points for the D-Bus adaptors (also used by tests).
    void handlePlay();
    void handlePause();
    void handlePlayPause();
    void handleStop();
    void handleSeekOffset(qlonglong offsetMicros);
    void handleSetPosition(const QString &trackObjectPath, qlonglong positionMicros);
    void handleSetVolume(double volume);
    void handleOpenUri(const QString &url);
    void handleQuit();
    void handleRaise();

private slots:
    void onMpvPlayingChanged();
    void onMpvPositionChanged();
    void onMpvDurationChanged();
    void onMpvVolumeChanged();

private:
    void setPlaying(bool playing);
    void setPosition(double seconds, bool emitSeeked);
    void setVolumeInternal(double volume);
    void setDuration(double seconds);
    void emitPropertiesChanged(const char *interface, const QVariantMap &changed);
    bool driveMpv() const;

    MpvController *m_mpv = nullptr;
    QWindow *m_raiseWindow = nullptr;
    bool m_registered = false;
    bool m_hasTrack = false;
    bool m_playing = false;
    bool m_mpvEffective = true;
    double m_positionSec = 0.0;
    double m_durationSec = 0.0;
    double m_volume = 1.0;
    QString m_videoId;
    QString m_title;
    QString m_channel;
    QString m_watchUrl;
    QString m_thumbnail;
    QString m_description;
};
