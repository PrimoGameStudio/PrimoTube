import QtQuick
import QtQuick.Controls

// Single reusable top-level player window. Created on demand by Main.qml
// and reloaded on every play request; closing it stops playback.
ApplicationWindow {
    id: window
    width: 960
    height: 600
    minimumWidth: 480
    minimumHeight: 320
    visible: false
    property var mainWindow: null
    title: playerPage.videoTitle.length > 0
        ? playerPage.videoTitle + " — PrimoTube"
        : qsTr("PrimoTube Player")

    function openVideo(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description) {
        // Backwards compat: allow the legacy 8-arg call (no channel id/url).
        if (videoWatchUrl === undefined && videoChannelUrl !== undefined) {
            description = thumbnail
            thumbnail = duration
            duration = durationString
            durationString = videoWatchUrl
            videoWatchUrl = videoChannelUrl
            videoChannelUrl = ""
            videoChannelId = ""
        }
        playerPage.stopPlayback()
        playerPage.videoId = videoId
        playerPage.videoTitle = videoTitle
        playerPage.videoChannel = videoChannel
        playerPage.videoChannelId = videoChannelId ?? ""
        playerPage.videoChannelUrl = videoChannelUrl ?? ""
        playerPage.videoWatchUrl = videoWatchUrl
        playerPage.videoDurationString = durationString ?? ""
        playerPage.videoDuration = duration ?? 0
        playerPage.videoThumbnail = thumbnail ?? ""
        playerPage.videoDescription = description ?? ""
        playerPage.errorText = ""
        playerPage.showDetails = false
        playerPage.showQueue = false
        playerPage.noticeText = ""
        playerPage.historyAdded = false
        playerPage.mpvFailed = false
        playerPage.resetShareButton()
        playerPage.pokeBars()
        playerPage.refreshCachedDur()
        // MPRIS: publish the new track; state updates follow via
        // PlayerPage.pushMprisState() as playback starts.
        mprisAdapter.setCurrentTrack(videoId, videoTitle, videoChannel, videoWatchUrl ?? "",
                                     thumbnail ?? "", duration ?? 0, description ?? "")
        mprisAdapter.setEffectiveState(false, 0, duration ?? 0,
                                       settingsManager.volume, playerPage.effectiveMpv)
        // Prefer mpv (DASH/HD + subtitles); fall back to QtMultimedia
        // progressive streams when libmpv is unavailable.
        // History is added once playback actually starts (see maybeAddHistory),
        // so failed lookups don't pollute the list.
        if (mpvController.available) {
            mpvController.load(videoWatchUrl && videoWatchUrl.length > 0 ? videoWatchUrl : videoId)
        } else {
            playbackResolver.resolve(videoId)
        }
        videoDetails.fetch(videoId)
        window.show()
        window.raise()
        window.requestActivate()
        // Non-OpenGL RHI (Vulkan/Metal): mpv cannot render. Flip to the
        // compat player automatically instead of a black surface.
        if (mpvController.available && !playerPage.effectiveMpv) {
            playerPage.fallbackToCompat(qsTr("mpv rendering unavailable — using fallback player"))
        }
    }

    onClosing: playerPage.stopPlayback()

    PlayerPage {
        id: playerPage
        anchors.fill: parent
        playerWindow: window
        onChannelClicked: function(channelId, channelUrl, channelName) {
            if (window.mainWindow && window.mainWindow.openChannelPage) {
                window.mainWindow.openChannelPage(channelId, channelUrl, channelName)
                window.mainWindow.raise()
                window.mainWindow.requestActivate()
            }
        }
    }
}
