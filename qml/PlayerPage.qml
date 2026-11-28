import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import PrimoTube 1.0

Page {
    id: page
    signal channelClicked(string channelId, string channelUrl, string channelName)
    property var pageTheme: null
    readonly property var effectiveTheme: pageTheme
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
    property bool useMpv: mpvController.available && !settingsManager.forceFallbackPlayer
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
        // QueueManager::itemRequested is handled in Main.qml, which routes it
        // through openPlayerWindow like any other play request.
        queueModel.advance()
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

    // Live switch between the libmpv and the built-in (QtMultimedia) player
    // when the user toggles the "always use the built-in player" setting.
    function switchPlayerBackend() {
        if (page.videoId.length === 0)
            return
        page.errorText = ""
        if (settingsManager.forceFallbackPlayer) {
            // libmpv -> built-in player.
            mpvController.stop()
            page.noticeText = qsTr("Switched to the built-in player — playback restarted")
            page.pokeBars()
            noticeTimer.restart()
            playbackResolver.resolve(page.videoId)
        } else {
            // Built-in player -> libmpv.
            playbackResolver.cancel()
            player.stop()
            player.source = ""
            if (mpvController.available && mpvVideo.supported) {
                page.mpvFailed = false
                page.noticeText = qsTr("Switched to mpv — playback restarted")
                page.pokeBars()
                noticeTimer.restart()
                mpvController.load(page.videoWatchUrl.length > 0 ? page.videoWatchUrl : page.videoId)
            } else {
                // No usable mpv rendering (non-OpenGL RHI): stay on the
                // built-in player and say why.
                page.noticeText = qsTr("mpv rendering unavailable — using fallback player")
                page.pokeBars()
                noticeTimer.restart()
            }
        }
        page.pushMprisState()
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
    // Stable model for the speed selector; parseFloat("1.25×") -> 1.25.
    property var speedModel: ["0.5×", "0.75×", "1×", "1.25×", "1.5×", "2×"]
    function speedIndex() {
        for (var i = 0; i < page.speedModel.length; ++i) {
            if (Math.abs(parseFloat(page.speedModel[i]) - settingsManager.speed) < 0.01)
                return i
        }
        return 2 // 1×
    }
    function stepSpeed(delta) {
        var idx = Math.max(0, Math.min(page.speedModel.length - 1, speedBox.currentIndex + delta))
        settingsManager.speed = parseFloat(page.speedModel[idx])
    }
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
    onEffectiveMpvChanged: {
        page.refreshCachedDur()
        page.pushMprisState()
        // RHI/graphics support can flip at runtime. Don't leave a hidden mpv
        // surface and an empty fallback player in that case.
        if (!effectiveMpv && page.videoId.length > 0 && mpvController.available
                && !settingsManager.forceFallbackPlayer && !page.mpvFailed)
            page.fallbackToCompat(qsTr("mpv rendering unavailable — using fallback player"))
    }

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
        mpvController.setSpeed(settingsManager.speed)
        player.playbackRate = settingsManager.speed
        speedBox.currentIndex = page.speedIndex()
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
            // The fallback player's AudioOutput.volume follows this via the
            // Binding below; only mpv needs an explicit push.
            if (page.effectiveMpv)
                mpvController.setVolume(settingsManager.volume)
        }
        function onSpeedChanged() {
            // Applies immediately on both playback paths; the persisted
            // setting is the single source of truth for speed.
            mpvController.setSpeed(settingsManager.speed)
            player.playbackRate = settingsManager.speed
            if (speedBox && !speedBox.popup.visible)
                speedBox.currentIndex = page.speedIndex()
        }
        function onQualityCapChanged() {
            if (qualityBox && !qualityBox.popup.visible)
                qualityBox.currentIndex = Math.max(0, page.qualityModel.indexOf(settingsManager.qualityCap))
        }
        function onForceFallbackPlayerChanged() {
            page.switchPlayerBackend()
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
            if (shortcutsPopup.opened)
                return // let Popup handle its own dismissal
            var w = Window.window
            if (w && w.visibility === Window.FullScreen)
                w.showNormal()
        }
    }
    function spaceFocusGuard() {
        if (shortcutsPopup.opened)
            return true
        if (subBox.activeFocus || audioBox.activeFocus || qualityBox.activeFocus
                || speedBox.activeFocus || seek.activeFocus || volumeSlider.activeFocus)
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
    function seekFocusGuard() {
        if (shortcutsPopup.opened)
            return true
        if (subBox.activeFocus || audioBox.activeFocus || qualityBox.activeFocus
                || speedBox.activeFocus || seek.activeFocus || volumeSlider.activeFocus)
            return true
        var item = Window.window ? Window.window.activeFocusItem : null
        // These controls consume arrow keys natively.
        return item instanceof Slider || item instanceof ComboBox
            || item instanceof SpinBox || item instanceof TextField
            || item instanceof TextArea
    }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.seekBy(-10) }
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.seekBy(10) }
    }
    Shortcut {
        sequence: "J"
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.seekBy(-10) }
    }
    Shortcut {
        sequence: "L"
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.seekBy(10) }
    }
    Shortcut {
        sequence: "["
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.stepSpeed(-1) }
    }
    Shortcut {
        sequence: "]"
        context: Qt.WindowShortcut
        onActivated: { if (!page.seekFocusGuard()) page.stepSpeed(1) }
    }
    Shortcut {
        sequence: "?"
        context: Qt.WindowShortcut
        onActivated: shortcutsPopup.open()
    }

    // In-app reference for the shortcuts above. Reachable via "?" or the
    // toolbar button; modal so it can't be triggered accidentally by Space.
    Popup {
        id: shortcutsPopup
        anchors.centerIn: parent
        width: Math.min(360, page.width - 32)
        height: 270
        modal: true
        padding: 16
        background: Rectangle {
            color: effectiveTheme.surfaceColor
            border.color: effectiveTheme.qccHighlight
            radius: 6
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Keyboard shortcuts")
                color: effectiveTheme.textColor
                font.bold: true
                font.pixelSize: 15
            }
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: effectiveTheme.textColor
                font.family: "monospace"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                text: "Space  —  Play / Pause\n"
                    + "← / J  —  Back 10 seconds\n"
                    + "→ / L  —  Forward 10 seconds\n"
                    + "[ / ]  —  Slower / Faster playback\n"
                    + "F11  —  Toggle fullscreen\n"
                    + "Esc  —  Exit fullscreen\n"
                    + "?  —  Show this help"
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                onClicked: shortcutsPopup.close()
            }
        }
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
                color: effectiveTheme.textColor
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
                    color: effectiveTheme.textColor
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
                                && !settingsManager.forceFallbackPlayer
                        onClicked: page.retryMpv()
                    }
                    Button {
                        text: qsTr("Open in Browser")
                        visible: page.videoWatchUrl.length > 0
                                 && clipboardHelper.isAllowedPlaybackTarget(page.videoWatchUrl)
                        onClicked: {
                            if (clipboardHelper.isAllowedPlaybackTarget(page.videoWatchUrl))
                                Qt.openUrlExternally(page.videoWatchUrl)
                        }
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
                    color: effectiveTheme.textColor
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
                color: effectiveTheme.isDark ? "#cc000000" : "#eeffffff"
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
                        color: effectiveTheme.textColor
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        // count is a NOTIFY property: referencing it makes the
                        // contains() call re-evaluate after add/remove.
                        text: {
                            favoritesModel.count
                            return favoritesModel.contains(page.videoId) ? qsTr("★ Favorited") : qsTr("☆ Favorite")
                        }
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
                                 && clipboardHelper.isAllowedPlaybackTarget(page.videoWatchUrl)
                        Accessible.name: qsTr("Copy share link")
                        ToolTip.text: qsTr("Copy share link")
                        ToolTip.visible: hovered
                        onClicked: {
                            if (!clipboardHelper.isAllowedPlaybackTarget(page.videoWatchUrl)) {
                                page.noticeText = qsTr("This link is not a YouTube video link.")
                                page.pokeBars()
                                noticeTimer.restart()
                            } else if (clipboardHelper.copyText(page.videoWatchUrl)) {
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
                        text: "?"
                        Accessible.name: qsTr("Keyboard shortcuts")
                        ToolTip.text: qsTr("Keyboard shortcuts (?)")
                        ToolTip.visible: hovered
                        onClicked: shortcutsPopup.open()
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
                color: effectiveTheme.isDark ? "#cc000000" : "#eeffffff"
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
                            color: channelMouse.containsMouse ? "#6cb9ff" : effectiveTheme.textColor
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
                                color: effectiveTheme.textColor
                                opacity: 0.7
                                font.pixelSize: 12
                            }
                            ComboBox {
                                id: subBox
                                Layout.preferredWidth: 150
                                model: page.subModel
                                currentIndex: 0
                                palette.text: effectiveTheme.textColor
                                palette.buttonText: effectiveTheme.textColor
                                palette.button: effectiveTheme.qccButton
                                palette.highlight: effectiveTheme.qccHighlight
                                palette.highlightedText: effectiveTheme.qccHighlightedText
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
                                color: effectiveTheme.textColor
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
                                palette.text: effectiveTheme.textColor
                                palette.buttonText: effectiveTheme.textColor
                                palette.button: effectiveTheme.qccButton
                                palette.highlight: effectiveTheme.qccHighlight
                                palette.highlightedText: effectiveTheme.qccHighlightedText
                                onActivated: function(idx) {
                                    mpvController.setAudioId(page.trackIdFromLabel(mpvController.audioTracks[idx]))
                                }
                                ToolTip.text: qsTr("Audio track")
                                ToolTip.visible: hovered
                            }
                            Label {
                                text: qsTr("Quality:")
                                color: effectiveTheme.textColor
                                opacity: 0.7
                                font.pixelSize: 12
                            }
                            ComboBox {
                                id: qualityBox
                                Layout.preferredWidth: 100
                                model: page.qualityModel
                                currentIndex: 0
                                palette.text: effectiveTheme.textColor
                                palette.buttonText: effectiveTheme.textColor
                                palette.button: effectiveTheme.qccButton
                                palette.highlight: effectiveTheme.qccHighlight
                                palette.highlightedText: effectiveTheme.qccHighlightedText
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
                        color: effectiveTheme.textColor
                        opacity: 0.8
                        font.pixelSize: 12
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: page.noticeText.length > 0
                        text: page.noticeText
                        color: effectiveTheme.isDark ? "#9fe6a0" : "#2e7d32"
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
                            color: effectiveTheme.textColor
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
                            palette.highlight: effectiveTheme.qccHighlight
                            palette.mid: effectiveTheme.qccMid
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
                            color: effectiveTheme.textColor
                            font.pixelSize: 12
                            opacity: 0.8
                        }
                        Slider {
                            id: volumeSlider
                            objectName: "volumeSlider"
                            Layout.preferredWidth: 110
                            from: 0
                            to: 1
                            palette.highlight: effectiveTheme.qccHighlight
                            palette.mid: effectiveTheme.qccMid
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
                                    audio.muted = false
                                }
                                // Binding drives audio.volume from this.
                                settingsManager.volume = value
                            }
                            ToolTip.text: qsTr("Volume")
                            ToolTip.visible: hovered
                        }
                        Label {
                            text: qsTr("Speed:")
                            color: effectiveTheme.textColor
                            font.pixelSize: 12
                            opacity: 0.8
                        }
                        ComboBox {
                            id: speedBox
                            objectName: "speedBox"
                            Layout.preferredWidth: 80
                            model: page.speedModel
                            currentIndex: 2
                            palette.text: effectiveTheme.textColor
                            palette.buttonText: effectiveTheme.textColor
                            palette.button: effectiveTheme.qccButton
                            palette.highlight: effectiveTheme.qccHighlight
                            palette.highlightedText: effectiveTheme.qccHighlightedText
                            onActivated: function(idx) {
                                settingsManager.speed = parseFloat(page.speedModel[idx])
                            }
                            ToolTip.text: qsTr("Playback speed ([ / ])")
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
                            color: effectiveTheme.textColor
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
                            color: effectiveTheme.textColor
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
                            color: effectiveTheme.textColor
                            opacity: 0.7
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            onLinkActivated: function(link) {
                                if (!link || link.length === 0)
                                    return
                                if (link.indexOf("watch?") >= 0 || link.indexOf("youtu.be/") >= 0) {
                                    if (!clipboardHelper.isAllowedPlaybackTarget(link))
                                        return
                                    var win = page.playerWindow
                                    var main = win && win.mainWindow
                                    if (main && main.forwardPlayRequested)
                                        main.forwardPlayRequested(link, "", "", "", "", link, "", 0, "", "")
                                } else if (link.indexOf("youtube.com/") >= 0) {
                                    // ChannelBackend re-validates the host/path.
                                    page.channelClicked("", link, "")
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: videoDetails.subtitleLangs.length > 0
                            text: qsTr("Subtitles: %1").arg(videoDetails.subtitleLangs.join(", "))
                            color: effectiveTheme.textColor
                            opacity: 0.7
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: videoDetails.description.length > 0 ? videoDetails.description : (page.videoDescription.length > 0 ? page.videoDescription : qsTr("No description available."))
                            color: effectiveTheme.isDark ? "#dddddd" : "#666666"
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
                            color: effectiveTheme.textColor
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
                                        sourceSize: Qt.size(160, 90)
                                        asynchronous: true
                                        cache: true
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
                                            color: effectiveTheme.textColor
                                            opacity: 0.7
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Button {
                                        text: qsTr("✕")
                                        Accessible.name: qsTr("Remove from queue")
                                        onClicked: queueModel.removeAt(index)
                                    }
                                }
                                onClicked: queueModel.playIndex(index)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: queueModel.count === 0
                            text: qsTr("Queue is empty.\nAdd videos from search or channel pages.")
                            color: effectiveTheme.textColor
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
                            color: effectiveTheme.textColor
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

    // Single one-way source of truth for the fallback player's volume.
    // Imperative assignments used to destroy this binding at runtime.
    // No `when` guard: the slider writes settingsManager.volume, so audio
    // must follow immediately even while the slider is being dragged.
    Binding {
        target: audio
        property: "volume"
        value: settingsManager.volume
    }

    Connections {
        target: playbackResolver
        function onResolved(url) {
            if (page.effectiveMpv)
                return
            page.errorText = ""
            player.source = url
            // A new source can reset the rate on some backends.
            player.playbackRate = settingsManager.speed
            player.play()
            page.maybeAddHistory()
        }
        function onResolveError(message) {
            if (!page.effectiveMpv)
                page.errorText = message
        }
    }

    // Video details arrive after openVideo() published the (possibly bare)
    // placeholder track. Refresh MPRIS metadata with the real title/channel;
    // this is what makes an MPRIS OpenUri show something useful in clients.
    Connections {
        target: videoDetails
        function onLoaded() {
            if (page.videoId.length === 0 || videoDetails.title.length === 0)
                return
            mprisAdapter.setCurrentTrack(page.videoId, videoDetails.title,
                                         videoDetails.channel.length > 0 ? videoDetails.channel : page.videoChannel,
                                         page.videoWatchUrl, page.videoThumbnail,
                                         page.videoDuration > 0 ? page.videoDuration : 0,
                                         videoDetails.description)
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
                audio.muted = false
                settingsManager.volume = volume
            }
        }
        function onRequestOpenUri(url) {
            // Route through the main open flow so metadata, MPRIS track,
            // history and the mpv/compat choice all follow the new video.
            if (!url || url.length === 0 || !clipboardHelper.isAllowedPlaybackTarget(url))
                return
            var win = page.playerWindow
            var main = win && win.mainWindow
            if (main && main.forwardPlayRequested) {
                main.forwardPlayRequested(url, "", "", "", "", url, "", 0, "", "")
            } else if (win) {
                win.openVideo(url, "", "", "", "", url, "", 0, "", "")
            }
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
