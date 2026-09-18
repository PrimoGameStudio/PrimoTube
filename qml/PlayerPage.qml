import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import PrimoTube 1.0

Page {
    id: page
    signal channelClicked(string channelId, string channelUrl, string channelName)
    property string videoId: ""
    property string videoTitle: ""
    property string videoChannel: ""
    property string videoChannelId: ""
    property string videoChannelUrl: ""
    property string videoWatchUrl: ""
    property string videoDurationString: ""
    property int videoDuration: 0
    property string videoThumbnail: ""
    property string videoDescription: ""
    property string errorText: ""
    property bool showDetails: false
    property string noticeText: ""
    property bool useMpv: mpvController.available
    // True only when mpv is both available and renderable; otherwise the
    // QtMultimedia fallback owns playback even if useMpv was snapshotted true.
    // mpvFailed flips to compat after an mpv error or non-GL RHI detection.
    property bool mpvFailed: false
    readonly property bool effectiveMpv: useMpv && mpvVideo.supported && !mpvFailed
    property bool historyAdded: false
    property var playerWindow: null
    property bool showQueue: false
    property bool autoPlay: true

    function resetShareButton() {
        shareButton.text = qsTr("⤴ Share")
    }

    function advanceQueue() {
        if (!page.autoPlay)
            return
        if (queueModel.advance()) {
            var item = queueModel.currentItem()
            if (item && item.videoId && playerWindow) {
                playerWindow.openVideo(item.videoId, item.title, item.channel, item.channelId ?? "", item.channelUrl ?? "", item.watchUrl, item.durationString ?? "", item.duration ?? 0, item.thumbnail ?? "", item.description ?? "")
            }
        }
    }

    function maybeAddHistory() {
        if (historyAdded || videoId.length === 0)
            return
        historyAdded = true
        historyModel.addItem(videoId, videoTitle, videoChannel, videoThumbnail, videoWatchUrl, videoDurationString, videoDuration, videoDescription, videoChannelId, videoChannelUrl)
    }

    function stopPlayback() {
        if (effectiveMpv) {
            mpvController.stop()
        } else {
            player.stop()
            player.source = ""
        }
        playbackResolver.cancel()
        videoDetails.cancel()
        historyAdded = false
        mprisAdapter.clearTrack()
    }

    // Reports the effective (mpv or fallback) playback state to the MPRIS
    // adapter so desktop media widgets stay in sync on both paths.
    // Position-only updates are cheap: the adapter does not emit
    // PropertiesChanged for those (clients poll Position).
    function pushMprisState() {
        mprisAdapter.setEffectiveState(page.isPlaying(), page.posSec(), page.durSec(),
                                       page.effectiveMpv ? mpvController.volume : audio.volume,
                                       page.effectiveMpv)
    }

    function fmt(sec) {
        if (sec === undefined || sec === null || sec < 0 || isNaN(sec))
            return "0:00"
        var s = Math.floor(sec)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var rem = s % 60
        var mm = h > 0 ? String(m).padStart(2, "0") : String(m)
        var ss = String(rem).padStart(2, "0")
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }

    function fmtMs(ms) {
        return page.fmt(ms / 1000)
    }

    function isPlaying() {
        return effectiveMpv ? mpvController.playing : player.playing
    }

    function posSec() {
        return effectiveMpv ? mpvController.position : player.position / 1000
    }

    function durSec() {
        var d = effectiveMpv ? mpvController.duration : player.duration / 1000
        if (!d || d <= 0)
            return videoDuration > 0 ? videoDuration : 0
        return d
    }

    function togglePlay() {
        if (effectiveMpv) {
            mpvController.togglePlay()
        } else if (player.playing) {
            player.pause()
        } else {
            player.play()
        }
    }

    function retryPlayback() {
        page.errorText = ""
        page.showDetails = false
        if (page.mpvFailed) {
            // Compat retry: stay on the fallback player.
            playbackResolver.resolve(page.videoId)
        } else if (effectiveMpv) {
            mpvController.load(page.videoWatchUrl.length > 0 ? page.videoWatchUrl : page.videoId)
        } else {
            playbackResolver.resolve(page.videoId)
        }
    }

    function retryMpv() {
        page.errorText = ""
        page.showDetails = false
        page.mpvFailed = false
        mpvController.load(page.videoWatchUrl.length > 0 ? page.videoWatchUrl : page.videoId)
    }

    function fallbackToCompat(reason) {
        if (page.mpvFailed)
            return
        page.mpvFailed = true
        mpvController.stop()
        page.noticeText = reason
        page.pokeBars()
        noticeTimer.restart()
        playbackResolver.resolve(page.videoId)
    }

    // Raw backend detail for the Details drawer: resolver stderr on the
    // compat path, mpv error otherwise (previously mpv errors hid it).
    function detailText() {
        if (playbackResolver.rawError.length > 0)
            return playbackResolver.rawError
        if (mpvController.lastError.length > 0)
            return mpvController.lastError
        return ""
    }

    function trackIdFromLabel(label) {
        if (!label || label === qsTr("Off"))
            return -1
        var m = /^#(-?\d+)/.exec(label)
        return m ? parseInt(m[1], 10) : -1
    }

    function subIndexFromId(sid) {
        if (sid === undefined || sid === null || sid < 0)
            return 0
        for (var i = 0; i < mpvController.subtitleTracks.length; ++i) {
            if (page.trackIdFromLabel(mpvController.subtitleTracks[i]) === sid)
                return i + 1 // offset 0 is "Off"
        }
        return 0
    }

    function audioIndexFromId(aid) {
        if (aid === undefined || aid === null || aid < 0)
            return -1
        for (var i = 0; i < mpvController.audioTracks.length; ++i) {
            if (page.trackIdFromLabel(mpvController.audioTracks[i]) === aid)
                return i
        }
        return -1
    }

    // Stable ComboBox models: rebuilding ["Off"].concat(...) inline creates
    // a new array identity per eval and resets the popup/selection. These
    // cached arrays are rebuilt only when the source lists change, and the
    // current index is set imperatively so an open popup is never stolen.
    property var subModel: ["Off"]
    property var qualityModel: []
    // Cached duration so the seek slider doesn't call durSec() per eval.
    property real cachedDur: 0
    function refreshCachedDur() {
        var d = page.durSec()
        page.cachedDur = d > 0 ? d : 0
    }

    function rebuildSubModel() {
        var popupOpen = subBox && subBox.popup.visible
        subModel = ["Off"].concat(mpvController.subtitleTracks)
        if (!popupOpen)
            subBox.currentIndex = page.subIndexFromId(mpvController.subtitleId)
    }
    function refreshSubIndex() {
        if (subBox && !subBox.popup.visible)
            subBox.currentIndex = page.subIndexFromId(mpvController.subtitleId)
    }
    function refreshAudioIndex() {
        if (audioBox && !audioBox.popup.visible)
            audioBox.currentIndex = page.audioIndexFromId(mpvController.audioId)
    }

    onErrorTextChanged: page.pokeBars()
    onVideoDurationChanged: page.refreshCachedDur()
    onEffectiveMpvChanged: { page.refreshCachedDur(); page.pushMprisState() }

    function pokeBars() {
        titleOverlay.opacity = 1
        controlsOverlay.opacity = 1
        var playing = page.isPlaying()
        if (playing && errorText.length === 0)
            hideTimer.restart()
        else
            hideTimer.stop()
    }

    function seekBy(deltaSec) {
        if (page.effectiveMpv) {
            mpvController.seek(Math.max(0, mpvController.position + deltaSec))
        } else if (player.duration > 0) {
            player.setPosition(Math.max(0, Math.min(player.duration, player.position + Math.round(deltaSec * 1000))))
        }
    }

    Component.onCompleted: {
        mpvController.setVolume(settingsManager.volume)
        // NOTE: audio.volume keeps its declarative binding
        // (audio.volume: settingsManager.volume) — do not assign here.
        page.qualityModel = settingsManager.qualityCapOptions()
        qualityBox.currentIndex = Math.max(0, page.qualityModel.indexOf(settingsManager.qualityCap))
        page.rebuildSubModel()
        page.refreshAudioIndex()
    }

    Connections {
        target: mpvController
        function onSubtitleTracksChanged() { page.rebuildSubModel() }
        function onSubtitleIdChanged() { page.refreshSubIndex() }
        function onAudioTracksChanged() { page.refreshAudioIndex() }
        function onAudioIdChanged() { page.refreshAudioIndex() }
        function onDurationChanged() { page.refreshCachedDur(); page.pushMprisState() }
        function onPlayingChanged() { page.pushMprisState() }
        function onPausedChanged() { page.pushMprisState() }
        function onPositionChanged() { page.pushMprisState() }
        function onVolumeChanged() { page.pushMprisState() }
    }

    Connections {
        target: settingsManager
        function onVolumeChanged() {
            if (page.effectiveMpv)
                mpvController.setVolume(settingsManager.volume)
            else
                audio.volume = settingsManager.volume
        }
        function onQualityCapChanged() {
            if (qualityBox && !qualityBox.popup.visible)
                qualityBox.currentIndex = Math.max(0, page.qualityModel.indexOf(settingsManager.qualityCap))
        }
    }

    // Observes mouse movement without stealing clicks from anything below.
    MouseArea {
        id: activityObserver
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onEntered: page.pokeBars()
        onPositionChanged: page.pokeBars()
    }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: {
            if (page.isPlaying() && page.errorText.length === 0
                    && !overlayHover.containsMouse && !controlsHover.containsMouse) {
                titleOverlay.opacity = 0
                controlsOverlay.opacity = 0
            } else {
                page.pokeBars()
            }
        }
    }

    function toggleFullscreen() {
        const w = Window.window
        if (!w)
            return
        if (w.visibility === Window.FullScreen)
            w.showNormal()
        else
            w.showFullScreen()
    }

    Shortcut {
        sequence: "F11"
        context: Qt.WindowShortcut
        onActivated: page.toggleFullscreen()
    }
    Shortcut {
        sequence: Qt.Key_Escape
        context: Qt.WindowShortcut
        onActivated: {
            var w = Window.window
            if (w && w.visibility === Window.FullScreen)
                w.showNormal()
        }
    }
    function spaceFocusGuard() {
        if (subBox.activeFocus || audioBox.activeFocus || qualityBox.activeFocus
                || seek.activeFocus || volumeSlider.activeFocus)
            return true
        var item = Window.window ? Window.window.activeFocusItem : null
        if (!item)
            return false
        // Controls that consume Space natively; don't double-fire play/pause.
        // Covers buttons, sliders, text inputs, combo/spin boxes, and
        // toggle controls whose Space activation would otherwise also toggle
        // playback.
        return item instanceof Button || item instanceof Slider
            || item instanceof TextField || item instanceof TextArea
            || item instanceof ComboBox || item instanceof SpinBox
            || item instanceof CheckBox || item instanceof Switch
            || item instanceof RadioButton
    }
    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        // Evaluated at activation time (not in `enabled`, which would not
        // re-evaluate when focus moves). Buttons/sliders/text inputs
        // consume Space natively; don't double-fire play/pause.
        onActivated: {
            if (!page.spaceFocusGuard())
                page.togglePlay()
        }
    }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        onActivated: page.seekBy(-10)
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        onActivated: page.seekBy(10)
    }
    Shortcut {
        sequence: "J"
        context: Qt.WindowShortcut
        onActivated: page.seekBy(-10)
    }
    Shortcut {
        sequence: "L"
        context: Qt.WindowShortcut
        onActivated: page.seekBy(10)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            clip: true

            // mpv path: DASH/HD + subtitles via libmpv.
            MpvVideo {
                id: mpvVideo
                anchors.fill: parent
                controller: mpvController
                visible: page.effectiveMpv
            }

            VideoOutput {
                id: videoOut
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
                visible: !page.effectiveMpv
            }

            // Fallback notice when mpv item can't render (non-OpenGL RHI).
            Label {
                anchors.centerIn: parent
                visible: page.useMpv && !mpvVideo.supported
                text: qsTr("mpv rendering unavailable — using fallback player")
                color: "white"
                opacity: 0.7
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: page.effectiveMpv ? mpvController.loading
                                     : (playbackResolver.resolving || player.mediaStatus === MediaPlayer.LoadingMedia)
                visible: running
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 10
                visible: page.errorText.length > 0

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: page.errorText
                    color: "white"
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Button {
                        text: qsTr("Retry")
                        visible: page.videoId.length > 0
                        onClicked: page.retryPlayback()
                    }
                    Button {
                        text: qsTr("Retry mpv")
                        visible: page.videoId.length > 0 && page.mpvFailed
                        onClicked: page.retryMpv()
                    }
                    Button {
                        text: qsTr("Open in Browser")
                        visible: page.videoWatchUrl.length > 0
                        onClicked: Qt.openUrlExternally(page.videoWatchUrl)
                    }
                    Button {
                        text: page.showDetails ? qsTr("Hide Details") : qsTr("Details")
                        visible: page.detailText().length > 0
                        onClicked: page.showDetails = !page.showDetails
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.showDetails && page.detailText().length > 0
                    text: page.detailText()
                    color: "#bbbbbb"
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Rectangle {
                id: titleOverlay
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 44
                color: "#cc000000"
                opacity: 1
                visible: opacity > 0.01
                // Disable children while fading out so near-transparent
                // buttons can't be clicked.
                enabled: opacity > 0.5
                Behavior on opacity { NumberAnimation { duration: 200 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: page.videoTitle + (page.effectiveMpv ? "" : qsTr(" (compat)"))
                        color: "white"
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        text: favoritesModel.contains(page.videoId) ? qsTr("★ Favorited") : qsTr("☆ Favorite")
                        enabled: page.videoId.length > 0
                        Accessible.name: qsTr("Toggle favorite")
                        ToolTip.text: qsTr("Toggle favorite")
                        ToolTip.visible: hovered
                        onClicked: {
                            if (favoritesModel.contains(page.videoId)) {
                                favoritesModel.removeItem(page.videoId)
                                page.noticeText = qsTr("Removed from favorites")
                            } else {
                                 favoritesModel.addItem(page.videoId, page.videoTitle, page.videoChannel, page.videoThumbnail, page.videoWatchUrl, page.videoDurationString, page.videoDuration, page.videoDescription, page.videoChannelId, page.videoChannelUrl)
                                page.noticeText = qsTr("Added to favorites")
                            }
                            page.pokeBars()
                            noticeTimer.restart()
                        }
                    }
                    ToolButton {
                        text: downloadBackend.downloading
                            ? qsTr("⤓ %1%").arg(Math.round(downloadBackend.progress * 100))
                            : qsTr("⤓ Download")
                        enabled: page.videoId.length > 0 && !playbackResolver.resolving
                        Accessible.name: qsTr("Download video")
                        ToolTip.text: qsTr("Download video")
                        ToolTip.visible: hovered
                        onClicked: {
                            if (downloadBackend.downloading)
                                downloadBackend.cancel()
                            else
                                downloadBackend.download(page.videoId)
                        }
                    }
                    ToolButton {
                        id: shareButton
                        text: qsTr("⤴ Share")
                        enabled: page.videoWatchUrl.length > 0
                        Accessible.name: qsTr("Copy share link")
                        ToolTip.text: qsTr("Copy share link")
                        ToolTip.visible: hovered
                        onClicked: {
                            if (clipboardHelper.copyText(page.videoWatchUrl)) {
                                shareButton.text = qsTr("✓ Copied")
                                copiedTimer.restart()
                                page.noticeText = qsTr("Link copied to clipboard")
                                page.pokeBars()
                                noticeTimer.restart()
                            } else {
                                page.noticeText = qsTr("Could not copy the link to the clipboard.")
                                page.pokeBars()
                                noticeTimer.restart()
                            }
                        }
                    }
                    ToolButton {
                        text: qsTr("☰ Queue (%1)").arg(queueModel.count)
                        Accessible.name: qsTr("Toggle play queue")
                        ToolTip.text: qsTr("Toggle play queue")
                        ToolTip.visible: hovered
                        onClicked: {
                            page.showQueue = !page.showQueue
                            if (page.showQueue)
                                page.showDetails = false
                        }
                    }
                    ToolButton {
                        text: Window.window && Window.window.visibility === Window.FullScreen
                            ? qsTr("⛶ Exit Fullscreen")
                            : qsTr("⛶ Fullscreen")
                        Accessible.name: qsTr("Toggle fullscreen")
                        ToolTip.text: qsTr("Toggle fullscreen (F11)")
                        ToolTip.visible: hovered
                        onClicked: page.toggleFullscreen()
                    }
                }

                // Tracks hovering over the bar itself (never grabs input,
                // so the buttons underneath stay clickable). Disabled while
                // fading out so the invisible bar keeps no hit-area.
                MouseArea {
                    id: overlayHover
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    enabled: titleOverlay.opacity > 0.5
                    onEntered: page.pokeBars()
                }
            }

            Rectangle {
                id: controlsOverlay
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                color: "#cc000000"
                opacity: 1
                visible: opacity > 0.01
                enabled: opacity > 0.5
                height: overlayContent.implicitHeight + 12
                Behavior on opacity { NumberAnimation { duration: 200 } }

                ColumnLayout {
                    id: overlayContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumHeight: 24
                            verticalAlignment: Text.AlignVCenter
                            visible: page.videoChannel.length > 0
                            text: page.videoChannel
                            color: channelMouse.containsMouse ? "#6cb9ff" : "white"
                            opacity: channelMouse.containsMouse ? 1.0 : 0.7
                            font.pixelSize: 12
                            font.underline: channelMouse.containsMouse
                            elide: Text.ElideRight

                            MouseArea {
                                id: channelMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    page.channelClicked(page.videoChannelId, page.videoChannelUrl, page.videoChannel)
                                }
                            }
                        }

                        RowLayout {
                            visible: page.effectiveMpv
                            spacing: 8
                            Label {
                                text: qsTr("Subtitles:")
                                color: "white"
                                opacity: 0.7
                                font.pixelSize: 12
                            }
                            ComboBox {
                                id: subBox
                                Layout.preferredWidth: 150
                                model: page.subModel
                                currentIndex: 0
                                onActivated: function(idx) {
                                    if (idx === 0)
                                        mpvController.setSubtitleId(-1)
                                    else
                                        mpvController.setSubtitleId(page.trackIdFromLabel(mpvController.subtitleTracks[idx - 1]))
                                }
                                ToolTip.text: qsTr("Subtitle track")
                                ToolTip.visible: hovered
                            }
                            Label {
                                text: qsTr("Audio:")
                                color: "white"
                                opacity: 0.7
                                font.pixelSize: 12
                                visible: audioBox.visible
                            }
                            ComboBox {
                                id: audioBox
                                Layout.preferredWidth: 180
                                visible: mpvController.audioTracks.length > 0
                                model: mpvController.audioTracks
                                currentIndex: -1
                                onActivated: function(idx) {
                                    mpvController.setAudioId(page.trackIdFromLabel(mpvController.audioTracks[idx]))
                                }
                                ToolTip.text: qsTr("Audio track")
                                ToolTip.visible: hovered
                            }
                            Label {
                                text: qsTr("Quality:")
                                color: "white"
                                opacity: 0.7
                                font.pixelSize: 12
                            }
                            ComboBox {
                                id: qualityBox
                                Layout.preferredWidth: 100
                                model: page.qualityModel
                                currentIndex: 0
                                onActivated: function(idx) {
                                    var cap = page.qualityModel[idx]
                                    settingsManager.qualityCap = cap
                                    mpvController.setQualityCap(cap)
                                    page.noticeText = qsTr("Quality %1 applies to the next video").arg(cap)
                                    page.pokeBars()
                                    noticeTimer.restart()
                                }
                                ToolTip.text: qsTr("Max quality (next video)")
                                ToolTip.visible: hovered
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !page.effectiveMpv && playbackResolver.resolving
                        text: playbackResolver.statusText.length > 0 ? playbackResolver.statusText : qsTr("Resolving stream URL…")
                        color: "white"
                        opacity: 0.8
                        font.pixelSize: 12
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: page.noticeText.length > 0
                        text: page.noticeText
                        color: "#9fe6a0"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ToolButton {
                            text: page.isPlaying() ? qsTr("❚❚") : qsTr("▶")
                            enabled: page.effectiveMpv ? true : player.source.toString().length > 0
                            Accessible.name: page.isPlaying() ? qsTr("Pause") : qsTr("Play")
                            ToolTip.text: qsTr("Play/Pause (Space)")
                            ToolTip.visible: hovered
                            onClicked: page.togglePlay()
                        }
                        Label {
                            text: page.effectiveMpv ? page.fmt(mpvController.position) : page.fmtMs(player.position)
                            color: "white"
                            font.pixelSize: 12
                            opacity: 0.8
                        }
                        Slider {
                            id: seek
                            objectName: "seekSlider"
                            Layout.fillWidth: true
                            from: 0
                            to: page.cachedDur > 0 ? page.cachedDur : 1
                            enabled: page.cachedDur > 0
                            // Update from playback only when the user is not
                            // dragging; otherwise the binding fights the drag.
                            Binding {
                                target: seek
                                property: "value"
                                value: page.posSec()
                                when: !seek.pressed
                            }
                            onMoved: {
                                if (!pressed)
                                    return
                                if (page.effectiveMpv)
                                    mpvController.seek(value)
                                else
                                    player.setPosition(Math.floor(value * 1000))
                            }
                        }
                        Label {
                            text: page.fmt(page.cachedDur)
                            color: "white"
                            font.pixelSize: 12
                            opacity: 0.8
                        }
                        Slider {
                            id: volumeSlider
                            objectName: "volumeSlider"
                            Layout.preferredWidth: 110
                            from: 0
                            to: 1
                            Binding {
                                target: volumeSlider
                                property: "value"
                                value: page.effectiveMpv ? mpvController.volume : audio.volume
                                when: !volumeSlider.pressed
                            }
                            onMoved: {
                                if (page.effectiveMpv) {
                                    mpvController.setVolume(value)
                                    mpvController.setMuted(false)
                                } else {
                                    audio.volume = value
                                    audio.muted = false
                                }
                                settingsManager.volume = value
                            }
                            ToolTip.text: qsTr("Volume")
                            ToolTip.visible: hovered
                        }
                    }

                    // Views/likes + Info toggle (shared by mpv and fallback paths).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: videoDetails.loading ? qsTr("Loading details…")
                                : (videoDetails.viewCount >= 0 ? qsTr("%1 views • %2 likes").arg(videoDetails.viewCount.toLocaleString()).arg(videoDetails.likeCount >= 0 ? videoDetails.likeCount.toLocaleString() : "–") : "")
                            color: "white"
                            opacity: 0.6
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: page.showDetails ? qsTr("Hide Info") : qsTr("Info")
                            onClicked: {
                                page.showDetails = !page.showDetails
                                if (page.showDetails)
                                    page.showQueue = false
                            }
                        }
                    }
                }

                // Same click-transparent hover tracking as the top bar.
                MouseArea {
                    id: controlsHover
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    enabled: controlsOverlay.opacity > 0.5
                    onEntered: page.pokeBars()
                }
            }

            // Details drawer: description + metadata.
            Rectangle {
                id: detailsPane
                anchors.right: parent.right
                anchors.top: titleOverlay.bottom
                anchors.bottom: controlsOverlay.top
                width: Math.min(parent.width * 0.42, 420)
                visible: page.showDetails
                color: "#ee000000"

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: detailsPane.width - 20
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            text: videoDetails.title.length > 0 ? videoDetails.title : page.videoTitle
                            color: "white"
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: {
                                var parts = []
                                if (videoDetails.channel.length > 0)
                                    parts.push(videoDetails.channel)
                                else if (page.videoChannel.length > 0)
                                    parts.push(page.videoChannel)
                                if (videoDetails.uploadDate.length > 0)
                                    parts.push(videoDetails.uploadDate)
                                if (videoDetails.viewCount >= 0)
                                    parts.push(qsTr("%1 views").arg(videoDetails.viewCount.toLocaleString()))
                                if (videoDetails.likeCount >= 0)
                                    parts.push(qsTr("%1 likes").arg(videoDetails.likeCount.toLocaleString()))
                                return parts.join(" • ")
                            }
                            color: "white"
                            opacity: 0.7
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            onLinkActivated: function(link) {
                                page.channelClicked(videoDetails.channelId, videoDetails.channelUrl, videoDetails.channel)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: videoDetails.subtitleLangs.length > 0
                            text: qsTr("Subtitles: %1").arg(videoDetails.subtitleLangs.join(", "))
                            color: "white"
                            opacity: 0.7
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: videoDetails.description.length > 0 ? videoDetails.description : (page.videoDescription.length > 0 ? page.videoDescription : qsTr("No description available."))
                            color: "#dddddd"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // Queue drawer: list of queued items.
            Rectangle {
                id: queuePane
                anchors.right: parent.right
                anchors.top: titleOverlay.bottom
                anchors.bottom: controlsOverlay.top
                width: Math.min(parent.width * 0.45, 440)
                visible: page.showQueue
                color: "#ee000000"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Play Queue (%1)").arg(queueModel.count)
                            color: "white"
                            font.bold: true
                        }
                        Button {
                            text: qsTr("Clear")
                            visible: queueModel.count > 0
                            onClicked: queueModel.clear()
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: queueListView
                            anchors.fill: parent
                            clip: true
                            model: queueModel
                            spacing: 2
                            delegate: ItemDelegate {
                                width: queueListView.width
                                background: Rectangle {
                                    color: (index === queueModel.currentIndex) ? "#33ffffff" : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: 8
                                    Image {
                                        Layout.preferredWidth: 80
                                        Layout.preferredHeight: 45
                                        source: model.thumbnail ?? ""
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label {
                                            Layout.fillWidth: true
                                            text: model.title ?? ""
                                            color: (index === queueModel.currentIndex) ? "#6cb9ff" : "white"
                                            font.bold: index === queueModel.currentIndex
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: model.channel ?? ""
                                            color: "white"
                                            opacity: 0.7
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Button {
                                        text: qsTr("✕")
                                        onClicked: queueModel.removeAt(index)
                                    }
                                }
                                onClicked: {
                                    queueModel.playIndex(index)
                                    var item = queueModel.currentItem()
                                    if (item && item.videoId && page.playerWindow) {
                                        page.playerWindow.openVideo(item.videoId, item.title, item.channel, item.channelId ?? "", item.channelUrl ?? "", item.watchUrl, item.durationString ?? "", item.duration ?? 0, item.thumbnail ?? "", item.description ?? "")
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: queueModel.count === 0
                            text: qsTr("Queue is empty.\nAdd videos from search or channel pages.")
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            opacity: 0.6
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Auto-play next video")
                            color: "white"
                            font.pixelSize: 12
                        }
                        Switch {
                            id: autoPlaySwitch
                            checked: page.autoPlay
                            onToggled: page.autoPlay = checked
                        }
                    }
                }
            }
        }

    }

    MediaPlayer {
        id: player
        videoOutput: videoOut
        // Single writer for settingsManager.volume is the slider below;
        // this binding is one-way (settings -> player) to avoid a loop.
        audioOutput: AudioOutput {
            id: audio
            volume: settingsManager.volume
            onVolumeChanged: page.pushMprisState()
        }
        onErrorOccurred: function(error, errorString) {
            if (!page.effectiveMpv)
                page.errorText = qsTr("Playback error: %1").arg(errorString)
        }
        onPlayingChanged: { page.pokeBars(); page.pushMprisState() }
        onPositionChanged: page.pushMprisState()
        onDurationChanged: { page.refreshCachedDur(); page.pushMprisState() }
        onMediaStatusChanged: {
            if (!page.effectiveMpv && player.mediaStatus === MediaPlayer.EndOfMedia) {
                page.advanceQueue()
            }
        }
    }

    Connections {
        target: playbackResolver
        function onResolved(url) {
            if (page.effectiveMpv)
                return
            page.errorText = ""
            player.source = url
            player.play()
            page.maybeAddHistory()
        }
        function onResolveError(message) {
            if (!page.effectiveMpv)
                page.errorText = message
        }
    }

    Connections {
        target: mpvController
        function onErrorOccurred(message) {
            if (page.effectiveMpv && !page.mpvFailed)
                page.fallbackToCompat(qsTr("mpv failed — trying compat player…"))
            else if (page.effectiveMpv)
                page.errorText = qsTr("Playback error: %1").arg(message)
        }
        function onPlayingChanged() {
            if (page.effectiveMpv && mpvController.playing)
                page.maybeAddHistory()
            page.pokeBars()
        }
        function onPausedChanged() { page.pokeBars() }
        function onEndOfFile() {
            if (page.effectiveMpv)
                page.advanceQueue()
        }
    }

    // MPRIS requests from desktop media widgets. The mpv path is driven
    // directly in C++; these only apply to the QtMultimedia fallback.
    Connections {
        target: mprisAdapter
        function onRequestPlay() { if (!page.effectiveMpv && player.source.toString().length > 0) player.play() }
        function onRequestPause() { if (!page.effectiveMpv) player.pause() }
        function onRequestToggle() { if (!page.effectiveMpv) page.togglePlay() }
        function onRequestStop() { if (!page.effectiveMpv) page.stopPlayback() }
        function onRequestSeek(positionSec) {
            if (!page.effectiveMpv && player.seekable)
                player.setPosition(Math.floor(positionSec * 1000))
        }
        function onRequestVolume(volume) {
            if (!page.effectiveMpv) {
                audio.volume = volume
                audio.muted = false
                settingsManager.volume = volume
            }
        }
        function onRequestOpenUri(url) {
            if (!page.effectiveMpv && url && url.length > 0)
                playbackResolver.resolve(url)
        }
    }

    Timer {
        id: noticeTimer
        interval: 6000
        onTriggered: page.noticeText = ""
    }

    Timer {
        id: copiedTimer
        interval: 1500
        onTriggered: shareButton.text = qsTr("⤴ Share")
    }

    Connections {
        target: downloadBackend
        function onDownloadFinished(path) {
            const name = path ? String(path).split("/").pop() : ""
            page.noticeText = name ? qsTr("Saved: %1").arg(name) : qsTr("Download complete")
            page.pokeBars()
            noticeTimer.restart()
        }
        // Route to the notice line so a download failure never clobbers a
        // playback error shown in errorText.
        function onDownloadError(message) {
            page.noticeText = message
            page.pokeBars()
            noticeTimer.restart()
        }
    }

    Component.onDestruction: {
        if (page.effectiveMpv)
            mpvController.stop()
        else
            player.stop()
    }
}
