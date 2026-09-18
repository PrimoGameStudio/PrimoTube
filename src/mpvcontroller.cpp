#include "mpvcontroller.h"

#include <QDebug>
#include <QMetaObject>
#include <QRegularExpression>

#include <mpv/client.h>

static void checkError(int err, const char *what)
{
    if (err < 0)
        qWarning() << "MpvController:" << what << "failed:" << mpv_error_string(err);
}

void MpvController::wakeupCallback(void *ctx)
{
    auto *self = static_cast<MpvController *>(ctx);
    QMetaObject::invokeMethod(self, "onMpvEvents", Qt::QueuedConnection);
}

MpvController::MpvController(QObject *parent)
    : QObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "MpvController: mpv_create failed";
        return;
    }
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "msg-level", "all=no");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "ytdl", "yes");
    // Best DASH (video+audio) with progressive fallback — this is what
    // unlocks 1080p+/4K where QtMultimedia only got SD progressive.
    mpv_set_option_string(m_mpv, "ytdl-format", "bv*+ba/b");
    // Subtitles: auto-load all (manual + auto-generated), fuzzy match.
    mpv_set_option_string(m_mpv, "sub-auto", "fuzzy");
    mpv_set_option_string(m_mpv, "subs-with-matching-audio", "yes");
    mpv_set_option_string(m_mpv, "slang", "en,en-US,en.*");
    mpv_set_option_string(m_mpv, "alang", "en,en-US,en.*");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "150M");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "50M");

    mpv_set_wakeup_callback(m_mpv, wakeupCallback, this);

    if (mpv_initialize(m_mpv) < 0) {
        qWarning() << "MpvController: mpv_initialize failed";
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    observeProperties();
    char *ver = mpv_get_property_string(m_mpv, "mpv-version");
    qInfo() << "MpvController: libmpv ready (" << (ver ? ver : "unknown") << ")";
    if (ver)
        mpv_free(ver);
}

MpvController::~MpvController()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }
}

void MpvController::observeProperties()
{
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
}

void MpvController::load(const QString &url)
{
    if (!m_mpv || url.trimmed().isEmpty())
        return;
    m_lastError.clear();
    m_eofEmitted = false;
    m_cacheStalled = false;
    m_coreIdleStalled = false;
    setLoading(true);
    // Issue play-state change via observe event; avoid optimistic emits here
    // so QML doesn't flicker before mpv confirms.
    // Quality cap applies to the video being loaded.
    mpv_set_option_string(m_mpv, "ytdl-format", ytdlFormatForCap(m_qualityCap).toUtf8().constData());
    const QByteArray u = url.trimmed().toUtf8();
    const char *args[] = {"loadfile", u.constData(), "replace", nullptr};
    checkError(mpv_command_async(m_mpv, 0, args), "loadfile");
}

void MpvController::play()
{
    setPaused(false);
}

void MpvController::pause()
{
    setPaused(true);
}

void MpvController::togglePlay()
{
    setPaused(!m_paused);
}

void MpvController::stop()
{
    if (!m_mpv)
        return;
    const char *args[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, args);
    setPlaying(false);
    setPosition(0);
    setLoading(false);
}

void MpvController::seek(double seconds)
{
    if (!m_mpv)
        return;
    // mpv_command_string copies its argument synchronously, so unlike the
    // previous QByteArray+constData()+async pattern there is no lifetime
    // hazard after return.
    const QByteArray cmd = QStringLiteral("seek %1 absolute").arg(seconds, 0, 'f', 1).toUtf8();
    checkError(mpv_command_string(m_mpv, cmd.constData()), "seek");
}

void MpvController::setPaused(bool p)
{
    if (!m_mpv)
        return;
    int flag = p ? 1 : 0;
    // Synchronous set: value is read before return, no dangling pointer.
    // No optimistic emit — the mpv observe event confirms the new state.
    checkError(mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag), "set pause");
}

void MpvController::setVolume(double v)
{
    const double clamped = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, clamped + 1.0))
        return;
    m_volume = clamped;
    emit volumeChanged();
    if (m_mpv) {
        double mpvVol = clamped * 100.0;
        checkError(mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &mpvVol), "set volume");
    }
}

void MpvController::setMuted(bool m)
{
    if (m_muted == m)
        return;
    m_muted = m;
    emit mutedChanged();
    if (m_mpv) {
        int flag = m ? 1 : 0;
        checkError(mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag), "set mute");
    }
}

void MpvController::setSubtitleId(int id)
{
    if (!m_mpv)
        return;
    int64_t sid = id;
    checkError(mpv_set_property(m_mpv, "sid", MPV_FORMAT_INT64, &sid), "set sid");
}

void MpvController::setAudioId(int id)
{
    if (!m_mpv)
        return;
    int64_t aid = id;
    checkError(mpv_set_property(m_mpv, "aid", MPV_FORMAT_INT64, &aid), "set aid");
}

QString MpvController::ytdlFormatForCap(const QString &cap)
{
    // "720p" -> 720, "4K" -> 2160; unknown/Auto -> best DASH with fallback.
    int height = 0;
    if (cap.compare(QStringLiteral("4K"), Qt::CaseInsensitive) == 0) {
        height = 2160;
    } else {
        QString num = cap;
        if (num.endsWith(QLatin1Char('p'), Qt::CaseInsensitive))
            num.chop(1);
        bool ok = false;
        const int parsed = num.toInt(&ok);
        height = ok && parsed > 0 ? parsed : 0;
    }
    if (height > 0) {
        return QStringLiteral("bv*[height<=%1]+ba/b[height<=%1]/b").arg(height);
    }
    return QStringLiteral("bv*+ba/b");
}

void MpvController::setQualityCap(const QString &cap)
{
    if (m_qualityCap == cap)
        return;
    m_qualityCap = cap.isEmpty() ? QStringLiteral("Auto") : cap;
    emit qualityCapChanged();
}

void MpvController::setSubtitleEnabled(bool enabled){
    if (!m_mpv)
        return;
    if (!enabled) {
        setSubtitleId(-1); // "no" subtitles
    } else if (!m_subTracks.isEmpty()) {
        // Labels are built as "#<id> ..." in parseTrackList; extract the id
        // with the same regex QML uses (trackIdFromLabel).
        int sid = -1;
        static const QRegularExpression re(QStringLiteral("^#(-?\\d+)"));
        const QRegularExpressionMatch m = re.match(m_subTracks.constFirst());
        if (m.hasMatch())
            sid = m.captured(1).toInt();
        if (sid >= 0)
            setSubtitleId(sid);
        else
            mpv_set_option_string(m_mpv, "sid", "auto");
    } else {
        // Re-enable auto selection
        mpv_set_option_string(m_mpv, "sid", "auto");
    }
}

void MpvController::onMpvEvents()
{
    if (!m_mpv)
        return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            const QString name = QString::fromUtf8(prop->name);
            if (name == QLatin1String("pause") && prop->format == MPV_FORMAT_FLAG) {
                const bool p = *static_cast<int *>(prop->data) != 0;
                m_paused = p;
                emit pausedChanged();
                setPlaying(!p);
            } else if (name == QLatin1String("time-pos") && prop->format == MPV_FORMAT_DOUBLE) {
                setPosition(*static_cast<double *>(prop->data));
            } else if (name == QLatin1String("duration") && prop->format == MPV_FORMAT_DOUBLE) {
                setDuration(*static_cast<double *>(prop->data));
            } else if (name == QLatin1String("volume") && prop->format == MPV_FORMAT_DOUBLE) {
                const double v = *static_cast<double *>(prop->data) / 100.0;
                if (!qFuzzyCompare(m_volume + 1.0, v + 1.0)) {
                    m_volume = qBound(0.0, v, 1.0);
                    emit volumeChanged();
                }
            } else if (name == QLatin1String("mute") && prop->format == MPV_FORMAT_FLAG) {
                m_muted = *static_cast<int *>(prop->data) != 0;
                emit mutedChanged();
            } else if (name == QLatin1String("track-list") && prop->format == MPV_FORMAT_NODE) {
                parseTrackList(*static_cast<mpv_node *>(prop->data));
            } else if (name == QLatin1String("sid") && prop->format == MPV_FORMAT_INT64) {
                m_subId = static_cast<int>(*static_cast<int64_t *>(prop->data));
                emit subtitleIdChanged();
            } else if (name == QLatin1String("aid") && prop->format == MPV_FORMAT_INT64) {
                m_audioId = static_cast<int>(*static_cast<int64_t *>(prop->data));
                emit audioIdChanged();
            } else if (name == QLatin1String("paused-for-cache")) {
                // Buffering state drives the loading indicator so stalls
                // show a spinner instead of a frozen frame. Both flags must
                // clear before loading clears (AND, not OR).
                if (prop->format == MPV_FORMAT_FLAG) {
                    m_cacheStalled = *static_cast<int *>(prop->data) != 0;
                    updateLoadingFromStallFlags();
                }
            } else if (name == QLatin1String("core-idle")) {
                if (prop->format == MPV_FORMAT_FLAG) {
                    m_coreIdleStalled = *static_cast<int *>(prop->data) != 0;
                    updateLoadingFromStallFlags();
                }
            } else if (name == QLatin1String("eof-reached") && prop->format == MPV_FORMAT_FLAG) {
                const bool eof = *static_cast<int *>(prop->data) != 0;
                if (eof && !m_eofEmitted) {
                    m_eofEmitted = true;
                    setPlaying(false);
                    emit endOfFile();
                }
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            m_cacheStalled = false;
            m_coreIdleStalled = false;
            setLoading(false);
            setPlaying(true);
            break;
        case MPV_EVENT_START_FILE:
            m_cacheStalled = false;
            m_coreIdleStalled = false;
            setLoading(true);
            break;
        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(event->data);
            setLoading(false);
            if (ef->reason == MPV_END_FILE_REASON_EOF) {
                if (!m_eofEmitted) {
                    m_eofEmitted = true;
                    setPlaying(false);
                    emit endOfFile();
                }
            } else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                setPlaying(false);
                m_lastError = QString::fromUtf8(mpv_error_string(ef->error));
                if (m_lastError.isEmpty())
                    m_lastError = tr("Playback failed.");
                emit errorOccurred(m_lastError);
            }
            break;
        }
        case MPV_EVENT_LOG_MESSAGE:
            break;
        default:
            break;
        }
    }
}

void MpvController::parseTrackList()
{
    mpv_node node{};
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return;
    parseTrackList(node);
    mpv_free_node_contents(&node);
}

void MpvController::parseTrackList(const mpv_node &node)
{
    QStringList subs;
    QStringList audios;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; ++i) {
            mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP)
                continue;
            QString type, title, lang;
            int64_t id = -1;
            for (int j = 0; j < entry.u.list->num; ++j) {
                const QString key = QString::fromUtf8(entry.u.list->keys[j]);
                mpv_node &val = entry.u.list->values[j];
                if (key == QLatin1String("type") && val.format == MPV_FORMAT_STRING)
                    type = QString::fromUtf8(val.u.string);
                else if (key == QLatin1String("id") && val.format == MPV_FORMAT_INT64)
                    id = val.u.int64;
                else if (key == QLatin1String("title") && val.format == MPV_FORMAT_STRING)
                    title = QString::fromUtf8(val.u.string);
                else if (key == QLatin1String("lang") && val.format == MPV_FORMAT_STRING)
                    lang = QString::fromUtf8(val.u.string);
            }
            QString label = lang.isEmpty() ? title : lang + (title.isEmpty() ? QString() : QStringLiteral(" — ") + title);
            if (label.isEmpty())
                label = QStringLiteral("#%1").arg(id);
            else
                label = QStringLiteral("#%1 %2").arg(id).arg(label);
            if (type == QLatin1String("sub"))
                subs.append(label);
            else if (type == QLatin1String("audio"))
                audios.append(label);
        }
    }
    // NOTE: caller owns `node` — the MPV_EVENT_PROPERTY_CHANGE payload is
    // owned by mpv, the mpv_get_property path frees its own copy above.
    if (m_subTracks != subs) {
        m_subTracks = subs;
        emit subtitleTracksChanged();
    }
    if (m_audioTracks != audios) {
        m_audioTracks = audios;
        emit audioTracksChanged();
    }
}

void MpvController::updateLoadingFromStallFlags()
{
    setLoading(m_cacheStalled || m_coreIdleStalled);
}

void MpvController::setPlaying(bool v)
{
    if (m_playing == v)
        return;
    m_playing = v;
    emit playingChanged();
}

void MpvController::setPosition(double v)
{
    if (qFuzzyCompare(m_position + 1.0, v + 1.0))
        return;
    m_position = v;
    emit positionChanged();
}

void MpvController::setDuration(double v)
{
    if (qFuzzyCompare(m_duration + 1.0, v + 1.0))
        return;
    m_duration = v;
    emit durationChanged();
}

void MpvController::setLoading(bool v)
{
    if (m_loading == v)
        return;
    m_loading = v;
    emit loadingChanged();
}
