import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Page {
    id: page
    property var pageTheme: null
    Theme { id: fallbackTheme }
    readonly property var effectiveTheme: pageTheme ?? fallbackTheme
    background: Rectangle { color: effectiveTheme.backgroundColor }
    signal playRequested(string videoId, string videoTitle, string videoChannel, string videoChannelId, string videoChannelUrl, string watchUrl, string durationString, int duration, string thumbnail, string description)
    signal channelClicked(string channelId, string channelUrl, string channelName)
    signal openSettingsRequested()

    // Single routing rule for both the Enter key and the Search button:
    // channel references open the channel page, everything else searches.
    function routeQuery(rawText) {
        var q = rawText.trim()
        if (q.length === 0)
            return
        if (channelBackend.isChannelUrl(q) || (q.startsWith("@") && !q.includes(" "))) {
            page.channelClicked("", q, q)
        } else {
            tabBar.currentIndex = 1
            searchBackend.search(q, countBox.value)
        }
    }

    Component.onCompleted: queryField.forceActiveFocus()

    header: ToolBar {
        background: Rectangle { color: effectiveTheme.surfaceColor }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                spacing: 8

                TextField {
                    id: queryField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search YouTube, paste video/playlist URL, or channel (@handle / URL)…")
                    palette.text: effectiveTheme.textColor
                    palette.placeholderText: effectiveTheme.isDark ? "#888" : "#666"
                    background: Rectangle {
                        color: effectiveTheme.backgroundColor
                        border.color: "#333"
                        radius: 4
                    }
                    onAccepted: page.routeQuery(text)
                }
                SpinBox {
                    id: countBox
                    from: 1
                    to: 25
                    value: settingsManager.maxResults
                    editable: true
                    onValueModified: settingsManager.maxResults = value
                    ToolTip.text: qsTr("Max results")
                    ToolTip.visible: hovered

                    Connections {
                        target: settingsManager
                        function onMaxResultsChanged() {
                            if (countBox.value !== settingsManager.maxResults)
                                countBox.value = settingsManager.maxResults
                        }
                    }
                }
                Button {
                    text: searchBackend.searching ? qsTr("Stop") : qsTr("Search")
                    enabled: queryField.text.trim().length > 0 || searchBackend.searching
                    onClicked: {
                        if (searchBackend.searching) {
                            searchBackend.cancel()
                        } else {
                            page.routeQuery(queryField.text)
                        }
                    }
                }
                ToolButton {
                    text: qsTr("⚙")
                    ToolTip.text: qsTr("Settings")
                    ToolTip.visible: hovered
                    onClicked: page.openSettingsRequested()
                }
            }

            TabBar {
                id: tabBar
                Layout.fillWidth: true
                background: Rectangle { color: effectiveTheme.surfaceColor }
                TabButton { text: qsTr("Home"); palette.buttonText: effectiveTheme.textColor }
                TabButton { text: qsTr("Search Results"); palette.buttonText: effectiveTheme.textColor }
                TabButton { text: qsTr("Watch History"); palette.buttonText: effectiveTheme.textColor }
                TabButton { text: qsTr("Favorites"); palette.buttonText: effectiveTheme.textColor }
                TabButton { text: qsTr("Channels"); palette.buttonText: effectiveTheme.textColor }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            visible: tabBar.currentIndex === 0
            Label {
                Layout.fillWidth: true
                text: homeFeedModel.rowCount() > 0 ? qsTr("Latest from your subscriptions (%1)").arg(homeFeedModel.rowCount()) : qsTr("Home")
                color: effectiveTheme.textColor
                font.bold: true
            }
            Button {
                text: homeFeedBackend.loading ? qsTr("Stop") : qsTr("Refresh")
                enabled: homeFeedBackend.loading || subscriptionsModel.rowCount() > 0
                ToolTip.text: homeFeedBackend.loading && homeFeedBackend.channelCount > 0
                    ? qsTr("Loading channel %1 of %2…").arg(homeFeedBackend.channelsDone + 1).arg(homeFeedBackend.channelCount)
                    : qsTr("Fetch the latest videos (3 per channel)")
                ToolTip.visible: hovered
                onClicked: {
                    if (homeFeedBackend.loading)
                        homeFeedBackend.cancel()
                    else
                        homeFeedBackend.refresh(subscriptionsModel.snapshot())
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: tabBar.currentIndex === 0 && (homeFeedBackend.loading || homeFeedBackend.statusText.length > 0)
            BusyIndicator {
                running: homeFeedBackend.loading
                visible: homeFeedBackend.loading
            }
            Label {
                Layout.fillWidth: true
                text: homeFeedBackend.statusText
                elide: Text.ElideRight
                color: homeFeedBackend.lastError.length > 0 ? "#d32f2f" : effectiveTheme.textColor
                opacity: 0.8
            }
            Button {
                text: qsTr("▶ Play All")
                visible: homeFeedModel.rowCount() > 0 && !homeFeedBackend.loading
                onClicked: queueModel.enqueueModel(homeFeedModel)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: tabBar.currentIndex === 1 && (searchBackend.searching || searchBackend.statusText.length > 0)
            BusyIndicator {
                running: searchBackend.searching
                visible: searchBackend.searching
            }
            Label {
                Layout.fillWidth: true
                text: searchBackend.statusText
                elide: Text.ElideRight
                color: searchBackend.lastError.length > 0 ? "#d32f2f" : effectiveTheme.textColor
                opacity: 0.8
            }
            Button {
                text: qsTr("▶ Play All")
                visible: videoModel.rowCount() > 0 && !searchBackend.searching
                onClicked: queueModel.enqueueModel(videoModel)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: tabBar.currentIndex > 1 && tabBar.currentIndex < 4
            Label {
                Layout.fillWidth: true
                text: tabBar.currentIndex === 2 ? qsTr("Recent Watch History (%1)").arg(historyModel.rowCount()) : qsTr("Favorite Videos (%1)").arg(favoritesModel.rowCount())
                color: effectiveTheme.textColor
                font.bold: true
            }
            Button {
                text: qsTr("Clear All")
                onClicked: {
                    if (tabBar.currentIndex === 2)
                        historyModel.clear()
                    else if (tabBar.currentIndex === 3)
                        favoritesModel.clear()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: tabBar.currentIndex === 4
            Label {
                Layout.fillWidth: true
                text: qsTr("Subscribed Channels (%1)").arg(subscriptionsModel.rowCount())
                color: effectiveTheme.textColor
                font.bold: true
            }
            Button {
                text: qsTr("Clear All")
                visible: subscriptionsModel.rowCount() > 0
                onClicked: subscriptionsModel.clear()
            }
        }

        // ListView 0: Home feed (recent videos from subscriptions)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabBar.currentIndex === 0
        ListView {
            id: homeFeedView
            anchors.fill: parent
            clip: true
            reuseItems: true
            cacheBuffer: 400
            model: homeFeedModel
            spacing: 2
            delegate: VideoDelegate {
                width: homeFeedView.width
                onClicked: page.playRequested(model.videoId, model.title, model.channel, model.channelId ?? "", model.channelUrl ?? "", model.watchUrl, model.durationString ?? "", model.duration ?? 0, model.thumbnail ?? "", model.description ?? "")
                onChannelClicked: function(channelId, channelUrl, channelName) {
                    page.channelClicked(channelId, channelUrl, channelName)
                }
            }
            }
                Label {
                    anchors.centerIn: parent
                    visible: homeFeedView.count === 0
                    text: {
                        if (subscriptionsModel.rowCount() === 0) return qsTr("No subscribed channels yet.\nClick a channel name or paste a channel link to subscribe,\nthen press Refresh here.")
                        if (homeFeedBackend.loading) return ""
                        if (homeFeedBackend.statusText.length > 0) return homeFeedBackend.statusText
                        return qsTr("Press Refresh to load the latest videos\nfrom your subscriptions (3 per channel).")
                    }
                    color: effectiveTheme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.6
                    wrapMode: Text.WordWrap
                    width: parent.width - 40
                }
            }

        // ListView 1: Videos (Search Results, History, Favorites)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabBar.currentIndex !== 0 && tabBar.currentIndex !== 4
        ListView {
            id: videoResultsView
            anchors.fill: parent
            clip: true
            reuseItems: true
            cacheBuffer: 400
            model: {
                if (tabBar.currentIndex === 2) return historyModel
                if (tabBar.currentIndex === 3) return favoritesModel
                return videoModel
            }
            spacing: 2
            delegate: VideoDelegate {
                width: videoResultsView.width
                onClicked: page.playRequested(model.videoId, model.title, model.channel, model.channelId ?? "", model.channelUrl ?? "", model.watchUrl, model.durationString ?? "", model.duration ?? 0, model.thumbnail ?? "", model.description ?? "")
                onChannelClicked: function(channelId, channelUrl, channelName) {
                    page.channelClicked(channelId, channelUrl, channelName)
                }
            }
            footer: Button {
                width: videoResultsView.width
                visible: tabBar.currentIndex === 1 && searchBackend.hasMore && !searchBackend.searching
                text: qsTr("Load more (%1 shown)").arg(searchBackend.resultCount)
                onClicked: searchBackend.loadMore()
            }
            }
                Label {
                    anchors.centerIn: parent
                    // Search tab gates on `searching` (async yt-dlp); history
                    // and favorites are local models so an empty count means
                    // genuinely empty, not loading.
                    visible: tabBar.currentIndex === 1
                        ? (videoResultsView.count === 0 && !searchBackend.searching)
                        : (videoResultsView.count === 0 && videoResultsView.visible)
                    text: {
                        if (tabBar.currentIndex === 2) return qsTr("No watch history yet.")
                        if (tabBar.currentIndex === 3) return qsTr("No favorite videos saved.")
                        return qsTr("Search for a video to begin.")
                    }
                    color: effectiveTheme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.6
                }
            }


        // ListView 2: Subscribed Channels
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabBar.currentIndex === 4
        ListView {
            id: channelResultsView
            anchors.fill: parent
            clip: true
            reuseItems: true
            cacheBuffer: 400
            model: subscriptionsModel
            spacing: 2
            delegate: ItemDelegate {
                width: channelResultsView.width
                contentItem: RowLayout {
                    spacing: 12
                    Item {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        Layout.alignment: Qt.AlignVCenter
                        Image {
                            id: subAvatar
                            anchors.fill: parent
                            source: model.avatar ?? ""
                            sourceSize: Qt.size(128, 128)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            visible: false
                        }
                        OpacityMask {
                            anchors.fill: parent
                            source: subAvatar
                            maskSource: Rectangle {
                                width: 64
                                height: 64
                                radius: 32
                                visible: false
                            }
                            visible: subAvatar.status === Image.Ready
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: subAvatar.status !== Image.Ready
                            text: "⊙"
                            font.pixelSize: 28
                            opacity: 0.4
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#333"
                            border.width: 1
                            radius: 32
                            opacity: 0.3
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            text: model.title ?? ""
                            color: effectiveTheme.textColor
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: (model.handle ?? "").length > 0
                            text: model.handle ?? ""
                            color: effectiveTheme.textColor
                            opacity: 0.7
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Button {
                        text: qsTr("Unsubscribe")
                        onClicked: subscriptionsModel.unsubscribe(model.channelId.length > 0 ? model.channelId : model.url)
                    }
                }
                onClicked: page.channelClicked(model.channelId ?? "", model.url ?? "", model.title ?? "")
            }
            }
                Label {
                    anchors.centerIn: parent
                    visible: channelResultsView.count === 0
                    text: qsTr("No subscribed channels yet.\nClick a channel name or paste a channel link to subscribe.")
                    color: effectiveTheme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.6
                }
            }

    }
}
