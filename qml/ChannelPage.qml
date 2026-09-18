import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import PrimoTube 1.0

Page {
    id: page
    property var pageTheme: null
    Theme { id: fallbackTheme }
    readonly property var effectiveTheme: pageTheme ?? fallbackTheme
    signal playRequested(string videoId, string videoTitle, string videoChannel, string videoChannelId, string videoChannelUrl, string watchUrl, string durationString, int duration, string thumbnail, string description)
    signal channelClicked(string channelId, string channelUrl, string channelName)
    signal backRequested()

    property string initialChannelQuery: ""
    property int openRequestId: 0
    // Raw query this page is showing. The backend is shared across stacked
    // pages, so on return we reload when it moved on to another channel.
    property string boundQuery: ""

    function subscriptionKey() {
        if (channelBackend.channelId.length > 0)
            return channelBackend.channelId
        if (channelBackend.channelUrl.length > 0)
            return channelBackend.channelUrl
        return ""
    }

    function openChannel(channelIdOrUrlOrName) {
        if (!channelIdOrUrlOrName || channelIdOrUrlOrName.length === 0)
            return
        page.openRequestId += 1
        page.boundQuery = channelIdOrUrlOrName
        channelVideoModel.clear()
        channelBackend.openChannel(channelIdOrUrlOrName)
    }

    // When this page becomes visible again after popping a channel pushed
    // on top of it, the shared backend still shows the top channel —
    // reload our own query instead of displaying stale data.
    onVisibleChanged: {
        if (visible && page.boundQuery.length > 0
                && channelBackend.currentRequest() !== page.boundQuery) {
            channelVideoModel.clear()
            channelBackend.openChannel(page.boundQuery)
        }
    }

    Component.onCompleted: {
        if (initialChannelQuery.length > 0)
            openChannel(initialChannelQuery)
    }

    // Backfills the stored subscription avatar once the channel profile
    // HTML arrives (subscribe() runs before the avatar is known).
    Connections {
        target: channelBackend
        function onChannelChanged() {
            var avatar = channelBackend.avatarUrl
            if (!avatar || avatar.length === 0)
                return
            if (channelBackend.channelId.length > 0)
                subscriptionsModel.updateAvatar(channelBackend.channelId, avatar)
            if (channelBackend.channelUrl.length > 0)
                subscriptionsModel.updateAvatar(channelBackend.channelUrl, avatar)
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            ToolButton {
                text: qsTr("‹ Back")
                onClicked: {
                    // Only cancel when this page owns the in-flight request;
                    // otherwise we'd kill the channel load of the page below.
                    if (channelBackend.loading
                            && channelBackend.currentRequest() === page.boundQuery)
                        channelBackend.cancel()
                    if (StackView.view)
                        StackView.view.pop()
                    else
                        page.backRequested()
                }
            }
            Label {
                Layout.fillWidth: true
                text: channelBackend.channelTitle.length > 0 ? channelBackend.channelTitle : qsTr("Channel")
                font.bold: true
                elide: Text.ElideRight
            }
            ToolButton {
                // subVersion forces re-evaluation when subscriptions change;
                // contains() alone has no change signal dependency.
                property int subVersion: subscriptionsModel.count
                text: {
                    subVersion // track dependency
                    var key = page.subscriptionKey()
                    if (key.length > 0 && subscriptionsModel.contains(key))
                        return qsTr("★ Subscribed")
                    return qsTr("☆ Subscribe")
                }
                enabled: channelBackend.channelTitle.length > 0
                onClicked: {
                    var key = page.subscriptionKey()
                    if (key.length > 0 && subscriptionsModel.contains(key)) {
                        subscriptionsModel.unsubscribe(key)
                        // Also drop the alternate key (id vs url) if present.
                        if (channelBackend.channelId.length > 0)
                            subscriptionsModel.unsubscribe(channelBackend.channelId)
                        if (channelBackend.channelUrl.length > 0)
                            subscriptionsModel.unsubscribe(channelBackend.channelUrl)
                        noticeText = qsTr("Unsubscribed")
                    } else {
                        subscriptionsModel.subscribe(channelBackend.channelId, channelBackend.channelTitle, channelBackend.channelHandle, channelBackend.channelUrl, channelBackend.avatarUrl)
                        noticeText = qsTr("Subscribed")
                    }
                    noticeTimer.restart()
                }
            }
        }
    }

    property string noticeText: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Channel header banner
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: channelBackend.channelTitle.length > 0 || channelBackend.loading

            Item {
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                Layout.alignment: Qt.AlignTop

                Image {
                    id: avatarImage
                    anchors.fill: parent
                    source: channelBackend.avatarUrl
                    sourceSize: Qt.size(144, 144)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: false
                    Accessible.name: qsTr("Channel avatar")
                }
                OpacityMask {
                    anchors.fill: parent
                    source: avatarImage
                    maskSource: Rectangle {
                        width: 72
                        height: 72
                        radius: 36
                        visible: false
                    }
                    visible: avatarImage.status === Image.Ready
                }
                // Placeholder glyph while loading or when the avatar fails.
                Label {
                    anchors.centerIn: parent
                    visible: avatarImage.status !== Image.Ready
                    text: "⊙"
                    font.pixelSize: 36
                    opacity: 0.4
                    Accessible.name: qsTr("Channel avatar placeholder")
                }
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: "#333"
                    border.width: 1
                    radius: 36
                    opacity: 0.4
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: channelBackend.channelTitle
                    font.bold: true
                    font.pixelSize: 18
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: channelBackend.subscriberText.length > 0 || channelBackend.channelHandle.length > 0
                    text: [channelBackend.channelHandle, channelBackend.subscriberText].filter(function(s){return s.length > 0}).join(" • ")
                    opacity: 0.7
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: channelBackend.description.length > 0
                    text: channelBackend.description.split("\n")[0]
                    opacity: 0.5
                    font.pixelSize: 12
                    maximumLineCount: 1
                    elide: Text.ElideRight
                    ToolTip.text: channelBackend.description
                    ToolTip.visible: channelMouseArea.containsMouse
                    MouseArea {
                        id: channelMouseArea
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        hoverEnabled: true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: channelBackend.loading || channelBackend.statusText.length > 0 || noticeText.length > 0
            BusyIndicator {
                running: channelBackend.loading
                visible: channelBackend.loading
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: channelBackend.statusText
                    color: channelBackend.lastError.length > 0 ? "#d32f2f" : palette.text
                    elide: Text.ElideRight
                    opacity: 0.8
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    visible: noticeText.length > 0
                    text: noticeText
                    color: "#2e7d32"
                    elide: Text.ElideRight
                    opacity: 0.9
                    font.pixelSize: 12
                }
            }
            Button {
                text: qsTr("▶ Play All")
                visible: channelVideoModel.rowCount() > 0 && !channelBackend.loading
                onClicked: queueModel.enqueueModel(channelVideoModel)
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        ListView {
            id: channelVideosView
            anchors.fill: parent
            clip: true
            model: channelVideoModel
            spacing: 2
            reuseItems: true
            cacheBuffer: 400
            delegate: VideoDelegate {
                width: channelVideosView.width
                onClicked: page.playRequested(model.videoId, model.title, model.channel, model.channelId ?? "", model.channelUrl ?? "", model.watchUrl, model.durationString ?? "", model.duration ?? 0, model.thumbnail ?? "", model.description ?? "")
                onChannelClicked: function(cid, curl, cname) {
                    page.channelClicked(cid, curl, cname)
                }
            }
            footer: Button {
                width: channelVideosView.width
                visible: channelBackend.hasMore && !channelBackend.loading
                text: qsTr("Load more (%1 shown)").arg(channelBackend.videoCount)
                onClicked: channelBackend.loadMore()
            }
        }
            Label {
                anchors.centerIn: parent
                visible: channelVideosView.count === 0 && !channelBackend.loading
                text: channelBackend.lastError.length > 0 ? channelBackend.lastError : qsTr("No uploads found.")
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.6
                wrapMode: Text.WordWrap
                width: parent.width - 40
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.verticalCenter
                anchors.topMargin: 44
                visible: channelVideosView.count === 0 && !channelBackend.loading
                    && channelBackend.lastError.length > 0 && page.boundQuery.length > 0
                text: qsTr("Retry")
                onClicked: page.openChannel(page.boundQuery)
            }
        }
    }

    Timer {
        id: noticeTimer
        interval: 3000
        onTriggered: page.noticeText = ""
    }
}
