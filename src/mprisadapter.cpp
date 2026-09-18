#include "mprisadapter.h"

#include <QCoreApplication>
#include <QWindow>
#include <QtLogging>
#include <QtMath>

#include "mpvcontroller.h"

#ifdef PRIMOTUBE_HAS_MPRIS
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#endif

#ifdef PRIMOTUBE_HAS_MPRIS
// org.mpris.MediaPlayer2 root interface.
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(MprisAdapter *parent)
        : QDBusAbstractAdaptor(parent)
        , m_adapter(parent)
    {
    }

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("PrimoTube"); }
    QString desktopEntry() const { return QStringLiteral("primotube"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("http"), QStringLiteral("https")}; }
    QStringList supportedMimeTypes() const { return {}; }

public Q_SLOTS:
    void Quit() { m_adapter->handleQuit(); }
    void Raise() { m_adapter->handleRaise(); }

private:
    MprisAdapter *m_adapter;
};

// org.mpris.MediaPlayer2.Player interface (minimal playback subset:
// no track list, Next/Previous unsupported).
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    explicit MprisPlayerAdaptor(MprisAdapter *parent)
        : QDBusAbstractAdaptor(parent)
        , m_adapter(parent)
    {
    }

    QString playbackStatus() const { return m_adapter->playbackStatus(); }
    QString loopStatus() const { return QStringLiteral("None"); }
    void setLoopStatus(const QString &) { /* fixed: no loop control */ }
    double rate() const { return 1.0; }
    void setRate(double) { /* fixed rate */ }
    bool shuffle() const { return false; }
    void setShuffle(bool) { /* no track list to shuffle */ }
    QVariantMap metadata() const { return m_adapter->metadata(); }
    double volume() const { return m_adapter->volume(); }
    void setVolume(double v) { m_adapter->handleSetVolume(v); }
    qlonglong position() const { return m_adapter->positionMicros(); }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return false; }
    bool canGoPrevious() const { return false; }
    bool canPlay() const { return m_adapter->hasTrack(); }
    bool canPause() const { return m_adapter->hasTrack(); }
    bool canSeek() const { return m_adapter->hasTrack(); }
    bool canControl() const { return true; }

public Q_SLOTS:
    void Next() { /* unsupported: CanGoNext=false */ }
    void Previous() { /* unsupported: CanGoPrevious=false */ }
    void Pause() { m_adapter->handlePause(); }
    void PlayPause() { m_adapter->handlePlayPause(); }
    void Stop() { m_adapter->handleStop(); }
    void Play() { m_adapter->handlePlay(); }
    void Seek(qlonglong offset) { m_adapter->handleSeekOffset(offset); }
    void SetPosition(const QDBusObjectPath &trackId, qlonglong pos)
    {
        m_adapter->handleSetPosition(trackId.path(), pos);
    }
    void OpenUri(const QString &uri) { m_adapter->handleOpenUri(uri); }

Q_SIGNALS:
    void Seeked(qlonglong Position);

private:
    MprisAdapter *m_adapter;
};
#endif // PRIMOTUBE_HAS_MPRIS

MprisAdapter::MprisAdapter(MpvController *mpvController, QObject *parent)
    : QObject(parent)
    , m_mpv(mpvController)
{
#ifdef PRIMOTUBE_HAS_MPRIS
    auto *rootAdaptor = new MprisRootAdaptor(this);
    Q_UNUSED(rootAdaptor);
    auto *playerAdaptor = new MprisPlayerAdaptor(this);
    // Relay internal state changes as D-Bus PropertiesChanged / Seeked.
    // Position itself is polled by clients, so only jumps emit Seeked.
    connect(this, &MprisAdapter::playbackStatusChanged, this, [this]() {
        emitPropertiesChanged("org.mpris.MediaPlayer2.Player",
                              {{QStringLiteral("PlaybackStatus"), playbackStatus()}});
    });
    connect(this, &MprisAdapter::metadataChanged, this, [this]() {
        emitPropertiesChanged("org.mpris.MediaPlayer2.Player",
                              {{QStringLiteral("Metadata"), metadata()},
                               {QStringLiteral("CanPlay"), hasTrack()},
                               {QStringLiteral("CanPause"), hasTrack()},
                               {QStringLiteral("CanSeek"), hasTrack()}});
    });
    connect(this, &MprisAdapter::volumeChanged, this, [this]() {
        emitPropertiesChanged("org.mpris.MediaPlayer2.Player",
                              {{QStringLiteral("Volume"), m_volume}});
    });
    connect(this, &MprisAdapter::seeked, playerAdaptor, &MprisPlayerAdaptor::Seeked);
#endif
    if (m_mpv) {
        connect(m_mpv, &MpvController::playingChanged, this, &MprisAdapter::onMpvPlayingChanged);
        connect(m_mpv, &MpvController::positionChanged, this, &MprisAdapter::onMpvPositionChanged);
        connect(m_mpv, &MpvController::durationChanged, this, &MprisAdapter::onMpvDurationChanged);
        connect(m_mpv, &MpvController::volumeChanged, this, &MprisAdapter::onMpvVolumeChanged);
    }
}

bool MprisAdapter::registerService()
{
#ifdef PRIMOTUBE_HAS_MPRIS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "MprisAdapter: no session bus, MPRIS disabled";
        return false;
    }
    if (!bus.registerService(serviceName())) {
        qWarning() << "MprisAdapter: service registration failed:" << bus.lastError().message();
        return false;
    }
    if (!bus.registerObject(objectPath(), this, QDBusConnection::ExportAdaptors)) {
        qWarning() << "MprisAdapter: object registration failed";
        bus.unregisterService(serviceName());
        return false;
    }
    m_registered = true;
    qInfo() << "MprisAdapter: registered" << serviceName();
    return true;
#else
    return false;
#endif
}

void MprisAdapter::setCurrentTrack(const QString &videoId, const QString &title,
                                   const QString &channel, const QString &watchUrl,
                                   const QString &thumbnail, double durationSec,
                                   const QString &description)
{
    const QString id = videoId.trimmed();
    if (id.isEmpty()) {
        clearTrack();
        return;
    }
    m_videoId = id;
    m_title = title.trimmed();
    m_channel = channel.trimmed();
    m_watchUrl = watchUrl.trimmed();
    m_thumbnail = thumbnail.trimmed();
    m_description = description.trimmed();
    m_hasTrack = true;
    m_positionSec = 0.0;
    m_durationSec = durationSec >= 0.0 ? durationSec : 0.0;
    emit metadataChanged();
    emit playbackStatusChanged();
}

void MprisAdapter::setEffectiveState(bool playing, double positionSec, double durationSec,
                                     double volume, bool mpvEffective)
{
    m_mpvEffective = mpvEffective;
    setPlaying(playing);
    if (durationSec > 0.0)
        setDuration(durationSec);
    setPosition(positionSec, false);
    setVolumeInternal(volume);
}

void MprisAdapter::clearTrack()
{
    if (!m_hasTrack && !m_playing && m_positionSec == 0.0)
        return;
    m_hasTrack = false;
    m_videoId.clear();
    m_title.clear();
    m_channel.clear();
    m_watchUrl.clear();
    m_thumbnail.clear();
    m_description.clear();
    m_durationSec = 0.0;
    setPlaying(false);
    setPosition(0.0, false);
    emit metadataChanged();
    emit playbackStatusChanged();
}

QString MprisAdapter::playbackStatus() const
{
    return statusForState(m_hasTrack, m_playing);
}

QVariantMap MprisAdapter::metadata() const
{
    QVariantMap map = buildMetadata(currentTrackId(), m_title, m_channel, m_watchUrl,
                                    m_thumbnail, durationMicros(), m_description);
#ifdef PRIMOTUBE_HAS_MPRIS
    // The spec requires mpris:trackid to be an object path, not a string.
    map[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(currentTrackId()));
#endif
    return map;
}

QString MprisAdapter::currentTrackId() const
{
    if (!m_hasTrack || m_videoId.isEmpty())
        return QString::fromLatin1(noTrackId());
    QString safe;
    safe.reserve(m_videoId.size());
    for (const QChar c : m_videoId) {
        const char ch = c.toLatin1();
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')
            safe.append(c);
        else
            safe.append(QLatin1Char('_'));
    }
    if (safe.isEmpty())
        return QString::fromLatin1(noTrackId());
    return QStringLiteral("/org/primotube/track/") + safe;
}

QString MprisAdapter::statusForState(bool hasTrack, bool playing)
{
    if (!hasTrack)
        return QStringLiteral("Stopped");
    return playing ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

qlonglong MprisAdapter::secondsToMicros(double seconds)
{
    if (!(seconds > 0.0))
        return 0;
    return qRound64(seconds * 1000000.0);
}

double MprisAdapter::microsToSeconds(qlonglong micros)
{
    if (micros <= 0)
        return 0.0;
    return static_cast<double>(micros) / 1000000.0;
}

double MprisAdapter::clampVolume(double v)
{
    if (!(v >= 0.0))
        return 0.0;
    return qBound(0.0, v, 1.0);
}

double MprisAdapter::clampPosition(double pos, double duration)
{
    if (!(pos >= 0.0))
        return 0.0;
    if (duration > 0.0)
        return qBound(0.0, pos, duration);
    return pos;
}

QVariantMap MprisAdapter::buildMetadata(const QString &trackObjectPath, const QString &title,
                                        const QString &channel, const QString &watchUrl,
                                        const QString &thumbnail, qlonglong lengthMicros,
                                        const QString &description)
{
    QVariantMap map;
    map[QStringLiteral("mpris:trackid")] = trackObjectPath;
    map[QStringLiteral("mpris:length")] = lengthMicros;
    map[QStringLiteral("xesam:title")] = title;
    map[QStringLiteral("xesam:artist")] =
        channel.isEmpty() ? QStringList() : QStringList{channel};
    map[QStringLiteral("xesam:url")] = watchUrl;
    map[QStringLiteral("mpris:artUrl")] = thumbnail;
    if (!description.isEmpty())
        map[QStringLiteral("xesam:comment")] = description;
    return map;
}

void MprisAdapter::handlePlay()
{
    if (driveMpv())
        m_mpv->play();
    else
        emit requestPlay();
}

void MprisAdapter::handlePause()
{
    if (driveMpv())
        m_mpv->pause();
    else
        emit requestPause();
}

void MprisAdapter::handlePlayPause()
{
    if (driveMpv())
        m_mpv->togglePlay();
    else
        emit requestToggle();
}

void MprisAdapter::handleStop()
{
    setPlaying(false);
    setPosition(0.0, true);
    if (driveMpv())
        m_mpv->stop();
    else
        emit requestStop();
}

void MprisAdapter::handleSeekOffset(qlonglong offsetMicros)
{
    handleSetPosition(currentTrackId(), positionMicros() + offsetMicros);
}

void MprisAdapter::handleSetPosition(const QString &trackObjectPath, qlonglong posMicros)
{
    if (!m_hasTrack || trackObjectPath != currentTrackId())
        return; // stale request for a previous track
    const double target = clampPosition(microsToSeconds(posMicros), m_durationSec);
    setPosition(target, true);
    if (driveMpv())
        m_mpv->seek(target);
    else
        emit requestSeek(target);
}

void MprisAdapter::handleSetVolume(double volume)
{
    const double v = clampVolume(volume);
    setVolumeInternal(v);
    if (driveMpv())
        m_mpv->setVolume(v);
    else
        emit requestVolume(v);
}

void MprisAdapter::handleOpenUri(const QString &url)
{
    const QString uri = url.trimmed();
    if (uri.isEmpty())
        return;
    // Placeholder track so clients show something until QML pushes the
    // real metadata through the normal openVideo() flow.
    setCurrentTrack(uri, uri, QString(), uri, QString(), 0.0);
    if (driveMpv())
        m_mpv->load(uri);
    else
        emit requestOpenUri(uri);
}

void MprisAdapter::handleQuit()
{
    QCoreApplication::quit();
}

void MprisAdapter::handleRaise()
{
    if (!m_raiseWindow)
        return;
    m_raiseWindow->show();
    m_raiseWindow->raise();
    m_raiseWindow->requestActivate();
}

void MprisAdapter::onMpvPlayingChanged()
{
    if (!m_mpv || !m_mpvEffective || !m_hasTrack)
        return;
    setPlaying(m_mpv->playing());
}

void MprisAdapter::onMpvPositionChanged()
{
    if (!m_mpv || !m_mpvEffective || !m_hasTrack)
        return;
    setPosition(m_mpv->position(), false);
}

void MprisAdapter::onMpvDurationChanged()
{
    if (!m_mpv || !m_mpvEffective || !m_hasTrack)
        return;
    setDuration(m_mpv->duration());
}

void MprisAdapter::onMpvVolumeChanged()
{
    if (!m_mpv || !m_mpvEffective)
        return;
    setVolumeInternal(m_mpv->volume());
}

void MprisAdapter::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    emit playbackStatusChanged();
}

void MprisAdapter::setPosition(double seconds, bool emitSeeked)
{
    if (!(seconds >= 0.0))
        seconds = 0.0;
    if (qFuzzyCompare(m_positionSec + 1.0, seconds + 1.0))
        return;
    m_positionSec = seconds;
    if (emitSeeked)
        emit seeked(secondsToMicros(seconds));
}

void MprisAdapter::setVolumeInternal(double volume)
{
    const double v = clampVolume(volume);
    if (qFuzzyCompare(m_volume + 1.0, v + 1.0))
        return;
    m_volume = v;
    emit volumeChanged();
}

void MprisAdapter::setDuration(double seconds)
{
    if (!(seconds >= 0.0))
        return;
    if (qFuzzyCompare(m_durationSec + 1.0, seconds + 1.0))
        return;
    m_durationSec = seconds;
    emit metadataChanged(); // mpris:length lives in Metadata
}

void MprisAdapter::emitPropertiesChanged(const char *interface, const QVariantMap &changed)
{
    if (!m_registered)
        return;
#ifdef PRIMOTUBE_HAS_MPRIS
    QDBusMessage msg = QDBusMessage::createSignal(
        objectPath(), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    msg.setArguments({QString::fromLatin1(interface), QVariant::fromValue(changed), QStringList()});
    QDBusConnection::sessionBus().send(msg);
#else
    Q_UNUSED(interface);
    Q_UNUSED(changed);
#endif
}

bool MprisAdapter::driveMpv() const
{
    return m_mpvEffective && m_mpv && m_mpv->available();
}

#ifdef PRIMOTUBE_HAS_MPRIS
#include "mprisadapter.moc"
#endif
