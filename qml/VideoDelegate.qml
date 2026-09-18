import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: root
    signal channelClicked(string channelId, string channelUrl, string channelName)

    contentItem: RowLayout {
        spacing: 10

        Image {
            Layout.preferredWidth: 160
            Layout.preferredHeight: 90
            Layout.alignment: Qt.AlignVCenter
            source: model.thumbnail ?? ""
            sourceSize: Qt.size(320, 180)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: "#333"
                border.width: 1
                opacity: 0.4
            }
            Label {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 4
                visible: (model.durationString ?? "").length > 0
                text: model.durationString ?? ""
                padding: 3
                background: Rectangle {
                    color: "black"
                    opacity: 0.75
                    radius: 3
                }
                color: "white"
                font.pixelSize: 11
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: model.title ?? ""
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Button {
                Layout.fillWidth: true
                visible: (model.channel ?? "").length > 0
                text: model.channel ?? ""
                flat: true
                // Flat button consumes the click so it doesn't also trigger
                // the delegate's onClicked (play). Left-align channel text.
                contentItem: Label {
                    text: parent.text
                    opacity: 0.7
                    elide: Text.ElideRight
                    color: parent.hovered ? palette.highlight : palette.windowText
                    font.underline: parent.hovered
                }
                background: Item {}
                // Keep row height tight.
                topPadding: 0
                bottomPadding: 0
                leftPadding: 0
                onClicked: root.channelClicked(model.channelId ?? "", model.channelUrl ?? "", model.channel ?? "")
            }
            Label {
                Layout.fillWidth: true
                visible: (model.channel ?? "").length === 0
                text: ""
                // Collapsed when empty so rows without a channel stay tight.
                Layout.preferredHeight: 0
            }
            Label {
                Layout.fillWidth: true
                visible: (model.description ?? "").length > 0
                text: (model.description ?? "").split("\n")[0]
                opacity: 0.5
                font.pixelSize: 12
                maximumLineCount: 1
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: (model.uploadDate ?? "").length > 0
                text: model.uploadDate ?? ""
                opacity: 0.45
                font.pixelSize: 11
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }

        // Action buttons (Enqueue / Play Next)
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 4
            Button {
                text: qsTr("＋ Queue")
                font.pixelSize: 11
                padding: 10
                Layout.preferredHeight: 36
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Add to queue")
                ToolTip.visible: hovered
                onClicked: {
                    queueModel.enqueue(model.videoId ?? "", model.title ?? "", model.channel ?? "", model.thumbnail ?? "", model.watchUrl ?? "", model.durationString ?? "", model.duration ?? 0, model.description ?? "", model.channelId ?? "", model.channelUrl ?? "")
                }
            }
            Button {
                text: qsTr("▶ Next")
                font.pixelSize: 11
                padding: 10
                Layout.preferredHeight: 36
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Play next")
                ToolTip.visible: hovered
                onClicked: {
                    queueModel.enqueueNext(model.videoId ?? "", model.title ?? "", model.channel ?? "", model.thumbnail ?? "", model.watchUrl ?? "", model.durationString ?? "", model.duration ?? 0, model.description ?? "", model.channelId ?? "", model.channelUrl ?? "")
                }
            }
        }
    }
}
