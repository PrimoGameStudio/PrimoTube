#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

struct mpv_handle;

// Long-lived libmpv owner. Created once in main() and exposed to QML as
// `mpvController`. MpvVideoItem (QQuickFramebufferObject) renders this
// handle's video output. If libmpv fails to init, `available` is false and
// QML falls back to the QtMultimedia player.
class MpvController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QStringList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(int subtitleId READ subtitleId WRITE setSubtitleId NOTIFY subtitleIdChanged)
    Q_PROPERTY(QStringList audioTracks READ audioTracks NOTIFY audioTracksChanged)
    Q_PROPERTY(int audioId READ audioId WRITE setAudioId NOTIFY audioIdChanged)
    Q_PROPERTY(QString qualityCap READ qualityCap WRITE setQualityCap NOTIFY qualityCapChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    explicit MpvController(QObject *parent = nullptr);
    ~MpvController() override;

    // Maps "Auto"/"720p"/"1080p"/"4K" to an mpv ytdl-format string.
    static QString ytdlFormatForCap(const QString &cap);

    bool available() const { return m_mpv != nullptr; }
    mpv_handle *handle() const { return m_mpv; }

    bool playing() const { return m_playing; }
    bool paused() const { return m_paused; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QStringList subtitleTracks() const { return m_subTracks; }
    int subtitleId() const { return m_subId; }
    QStringList audioTracks() const { return m_audioTracks; }
    int audioId() const { return m_audioId; }
    QString qualityCap() const { return m_qualityCap; }
    QString lastError() const { return m_lastError; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void load(const QString &url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);
    // NOTE: WRITE accessors of Q_PROPERTY are NOT callable as functions
    // from QML — these must stay Q_INVOKABLE (QML calls them as
    // mpvController.setVolume(x), not via property assignment).
    Q_INVOKABLE void setPaused(bool p);
    Q_INVOKABLE void setVolume(double v); // 0.0..1.0
    Q_INVOKABLE void setMuted(bool m);
    Q_INVOKABLE void setSubtitleId(int id); // mpv sid, -1 = none
    Q_INVOKABLE void setAudioId(int id);
    Q_INVOKABLE void setQualityCap(const QString &cap); // takes effect on next load()

    Q_INVOKABLE void setSubtitleEnabled(bool enabled);

signals:
    void playingChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void subtitleTracksChanged();
    void subtitleIdChanged();
    void audioTracksChanged();
    void audioIdChanged();
    void qualityCapChanged();
    void errorOccurred(const QString &message);
    void loadingChanged();
    void endOfFile();

private slots:
    void onMpvEvents();

private:
    void setPlaying(bool v);
    void setPosition(double v);
    void setDuration(double v);
    void setLoading(bool v);
    void observeProperties();
    void parseTrackList();
    void parseTrackList(const struct mpv_node &node);
    void updateLoadingFromStallFlags();
    static void wakeupCallback(void *ctx);

    mpv_handle *m_mpv = nullptr;
    bool m_playing = false;
    bool m_paused = false;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_volume = 1.0;
    bool m_muted = false;
    QStringList m_subTracks;
    int m_subId = -1;
    QStringList m_audioTracks;
    int m_audioId = -1;
    QString m_qualityCap = QStringLiteral("Auto");
    QString m_lastError;
    bool m_loading = false;
    bool m_eofEmitted = false;
    bool m_cacheStalled = false;
    bool m_coreIdleStalled = false;
};
