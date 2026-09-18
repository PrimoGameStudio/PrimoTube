import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 700
    title: qsTr("PrimoTube")

    Component.onCompleted: {
        // Silent background check; banner appears only if an update exists.
        ytdlpUpdater.checkForUpdates()
        // Restore persisted theme (default is dark).
        theme.isDark = settingsManager.theme !== "light"
    }

    Connections {
        target: ytdlpUpdater
        function onUpdateAvailableChanged() {
            if (ytdlpUpdater.updateAvailable)
                root.updateDismissed = false
        }
    }

    // Two-way sync between the in-memory Theme object and the persisted
    // settingsManager.theme ("dark"/"light"), guarded against loops.
    Connections {
        target: theme
        function onIsDarkChanged() {
            var want = theme.isDark ? "dark" : "light"
            if (settingsManager.theme !== want)
                settingsManager.theme = want
        }
    }
    Connections {
        target: settingsManager
        function onThemeChanged() {
            var wantDark = settingsManager.theme !== "light"
            if (theme.isDark !== wantDark)
                theme.isDark = wantDark
        }
    }

    property var playerWindow: null
    property bool dependencyDismissed: false
    property bool updateDismissed: false
    
    Theme {
        id: theme
    }

    Component {
        id: playerWindowComponent
        PlayerWindow {
            mainWindow: root
        }
    }

    Component {
        id: settingsPageComponent
        SettingsPage {
            pageTheme: theme
            onBackRequested: function() {
                stackView.pop()
            }
        }
    }

    Component {
        id: channelPageComponent
        ChannelPage {
            pageTheme: theme
            onPlayRequested: function(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description) {
                root.forwardPlayRequested(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description)
            }
            onChannelClicked: function(channelId, channelUrl, channelName) {
                root.openChannelPage(channelId, channelUrl, channelName)
            }
            onBackRequested: function() {
                stackView.pop()
            }
        }
    }

    // Single forwarding rule for play requests from any page.
    function forwardPlayRequested(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description) {
        // Backwards compat: older call sites omit channel id/url (8-arg form
        // where videoChannel is followed directly by watchUrl).
        if (videoWatchUrl === undefined && videoChannelUrl !== undefined) {
            description = thumbnail
            thumbnail = duration
            duration = durationString
            durationString = videoWatchUrl
            videoWatchUrl = videoChannelUrl
            videoChannelUrl = ""
            videoChannelId = ""
        }
        root.openPlayerWindow(videoId, videoTitle, videoChannel, videoChannelId ?? "", videoChannelUrl ?? "", videoWatchUrl, durationString, duration, thumbnail, description ?? "")
    }

    function openPlayerWindow(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description) {
        queueModel.clear()
        queueModel.enqueue(videoId, videoTitle, videoChannel, thumbnail ?? "", videoWatchUrl ?? "", durationString ?? "", duration ?? 0, description ?? "", videoChannelId ?? "", videoChannelUrl ?? "")
        if (!root.playerWindow)
            root.playerWindow = playerWindowComponent.createObject(root)
        root.playerWindow.openVideo(videoId, videoTitle, videoChannel, videoChannelId ?? "", videoChannelUrl ?? "", videoWatchUrl, durationString, duration, thumbnail, description ?? "")
    }

    function openChannelPage(channelId, channelUrl, channelName) {
        var cid = channelId ?? ""
        var curl = channelUrl ?? ""
        var cname = channelName ?? ""
        var query = cid.length > 0 ? cid : (curl.length > 0 ? curl : cname.trim())
        if (query.length === 0)
            return
        // Dedup: don't push the same channel twice in a row.
        var cur = stackView.currentItem
        if (cur && cur.initialChannelQuery !== undefined && cur.initialChannelQuery === query)
            return
        // Bound stack growth for channel-in-channel navigation.
        if (stackView.depth > 6)
            stackView.replace(channelPageComponent, { initialChannelQuery: query })
        else
            stackView.push(channelPageComponent, { initialChannelQuery: query })
    }

    function openSettingsPage() {
        // Avoid stacking duplicates.
        var cur = stackView.currentItem
        if (cur && cur.objectName === "settingsPage")
            return
        stackView.push(settingsPageComponent)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: dependencyBanner
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? bannerLayout.implicitHeight + 16 : 0
            Layout.topMargin: visible ? 0 : 0
            Layout.bottomMargin: 0
            color: theme.isDark ? "#5a4a15" : "#fff3cd"
            visible: !root.dependencyDismissed && dependencyChecker.missingMessage.length > 0
            clip: true

            RowLayout {
                id: bannerLayout
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: dependencyChecker.missingMessage
                    color: theme.isDark ? "#ffe69c" : "#856404"
                    wrapMode: Text.WordWrap
                }

                Button {
                    text: qsTr("Dismiss")
                    onClicked: root.dependencyDismissed = true
                }
            }
        }

        Rectangle {
            id: updateBanner
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? updateLayout.implicitHeight + 16 : 0
            color: theme.isDark ? "#1b4324" : "#d4edda"
            visible: !root.updateDismissed && ytdlpUpdater.updateAvailable
            clip: true

            RowLayout {
                id: updateLayout
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: ytdlpUpdater.statusText
                    color: theme.isDark ? "#c3e6cb" : "#155724"
                    wrapMode: Text.WordWrap
                }

                Button {
                    text: ytdlpUpdater.updating ? qsTr("Updating…") : qsTr("Update")
                    enabled: !ytdlpUpdater.updating
                    onClicked: ytdlpUpdater.updateYtdlp()
                }
                Button {
                    text: qsTr("Dismiss")
                    onClicked: root.updateDismissed = true
                }
            }
        }

        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: SearchPage {
                pageTheme: theme
                onPlayRequested: function(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description) {
                    root.forwardPlayRequested(videoId, videoTitle, videoChannel, videoChannelId, videoChannelUrl, videoWatchUrl, durationString, duration, thumbnail, description)
                }
                onChannelClicked: function(channelId, channelUrl, channelName) {
                    root.openChannelPage(channelId, channelUrl, channelName)
                }
                onOpenSettingsRequested: function() {
                    root.openSettingsPage()
                }
            }

            pushEnter: Transition {
                PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 250 }
                PropertyAnimation { property: "x"; from: 50; to: 0; duration: 250 }
            }
            pushExit: Transition {
                PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 250 }
                PropertyAnimation { property: "x"; from: 0; to: -50; duration: 250 }
            }
            popEnter: Transition {
                PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 250 }
                PropertyAnimation { property: "x"; from: -50; to: 0; duration: 250 }
            }
            popExit: Transition {
                PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 250 }
                PropertyAnimation { property: "x"; from: 0; to: 50; duration: 250 }
            }
        }
    }
}
