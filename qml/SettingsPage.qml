import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Page {
    id: page
    objectName: "settingsPage"
    property var pageTheme: null
    Theme { id: fallbackTheme }
    readonly property var effectiveTheme: pageTheme ?? fallbackTheme
    signal backRequested()

    property string dirError: ""
    property string qualityNotice: ""
    property string exportNotice: ""
    // Stable model: calling qualityCapOptions() inline rebuilds the array
    // identity per eval and resets the ComboBox popup.
    property var qualityModel: []

    background: Rectangle { color: effectiveTheme.backgroundColor }

    function folderUrlToPath(url) {
        return settingsManager.urlToLocalFile(url)
    }

    function applyDir(path) {
        var p = path.trim()
        if (p.length === 0) {
            dirError = qsTr("Enter a folder path.")
            return
        }
        if (settingsManager.trySetDownloadDirectory(p)) {
            dirError = ""
            dirField.text = settingsManager.downloadDirectory
        } else {
            dirError = qsTr("Could not use that folder (it must be creatable and writable).")
        }
    }

    Component.onCompleted: {
        dirField.text = settingsManager.downloadDirectory
        page.qualityModel = settingsManager.qualityCapOptions()
        qualityBox.currentIndex = Math.max(0, page.qualityModel.indexOf(settingsManager.qualityCap))
    }

    Connections {
        target: settingsManager
        function onDownloadDirectoryChanged() {
            if (!dirField.activeFocus && dirField.text !== settingsManager.downloadDirectory)
                dirField.text = settingsManager.downloadDirectory
        }
        function onQualityCapChanged() {
            if (qualityBox && !qualityBox.popup.visible)
                qualityBox.currentIndex = Math.max(0, page.qualityModel.indexOf(settingsManager.qualityCap))
        }
    }

    header: ToolBar {
        background: Rectangle { color: effectiveTheme.surfaceColor }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            ToolButton {
                text: qsTr("‹ Back")
                onClicked: {
                    if (StackView.view)
                        StackView.view.pop()
                    else
                        page.backRequested()
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Settings")
                color: effectiveTheme.textColor
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 8
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            GroupBox {
                title: qsTr("Downloads")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Videos are saved to this folder (applies to the next download).")
                        color: effectiveTheme.textColor
                        opacity: 0.7
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: dirField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Download folder…")
                            palette.text: effectiveTheme.textColor
                            onAccepted: page.applyDir(text)
                        }
                        Button {
                            text: qsTr("Browse…")
                            onClicked: folderDialog.open()
                        }
                        Button {
                            text: qsTr("Apply")
                            onClicked: page.applyDir(dirField.text)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            text: qsTr("Reset to system default")
                            onClicked: {
                                var d = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                                if (d.length > 0)
                                    page.applyDir(d)
                                else
                                    page.dirError = qsTr("Could not locate the system download folder.")
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.dirError.length > 0
                        text: page.dirError
                        color: "#d32f2f"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            GroupBox {
                title: qsTr("Playback")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: qsTr("Max quality:")
                            color: effectiveTheme.textColor
                        }
                        ComboBox {
                            id: qualityBox
                            Layout.preferredWidth: 120
                            model: page.qualityModel
                            currentIndex: 0
                            onActivated: function(idx) {
                                var cap = page.qualityModel[idx]
                                settingsManager.qualityCap = cap
                                mpvController.setQualityCap(cap)
                                page.qualityNotice = qsTr("Quality %1 applies to the next video.").arg(cap)
                                qualityTimer.restart()
                            }
                            ToolTip.text: qsTr("Max quality (next video)")
                            ToolTip.visible: hovered
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.qualityNotice.length > 0
                        text: page.qualityNotice
                        color: "#2e7d32"
                        font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: qsTr("Volume:")
                            color: effectiveTheme.textColor
                        }
                        Slider {
                            id: volumeSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            // Single-writer rule: this slider writes
                            // settingsManager.volume; PlayerPage syncs from it.
                            Binding {
                                target: volumeSlider
                                property: "value"
                                value: settingsManager.volume
                                when: !volumeSlider.pressed
                            }
                            onMoved: settingsManager.volume = value
                        }
                        Label {
                            text: qsTr("%1%").arg(Math.round(settingsManager.volume * 100))
                            color: effectiveTheme.textColor
                            font.pixelSize: 12
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Search")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Max results per search:")
                        color: effectiveTheme.textColor
                    }
                    SpinBox {
                        id: resultsBox
                        from: 1
                        to: 25
                        value: settingsManager.maxResults
                        editable: true
                        onValueModified: settingsManager.maxResults = value
                        Connections {
                            target: settingsManager
                            function onMaxResultsChanged() {
                                if (resultsBox.value !== settingsManager.maxResults)
                                    resultsBox.value = settingsManager.maxResults
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Appearance")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Dark theme")
                        color: effectiveTheme.textColor
                    }
                    Switch {
                        // Persisted via settingsManager.theme; Main.qml syncs
                        // the in-memory Theme object from it.
                        checked: effectiveTheme.isDark
                        onToggled: settingsManager.theme = checked ? "dark" : "light"
                    }
                }
            }

            GroupBox {
                title: qsTr("Data & Backups")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Export watch history, favorites, or subscriptions to clipboard as JSON.")
                        color: effectiveTheme.textColor
                        opacity: 0.7
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            text: qsTr("Export History")
                            onClicked: {
                                clipboardHelper.copyText(historyModel.exportJson())
                                page.exportNotice = qsTr("Watch history copied to clipboard.")
                                exportTimer.restart()
                            }
                        }
                        Button {
                            text: qsTr("Export Favorites")
                            onClicked: {
                                clipboardHelper.copyText(favoritesModel.exportJson())
                                page.exportNotice = qsTr("Favorites copied to clipboard.")
                                exportTimer.restart()
                            }
                        }
                        Button {
                            text: qsTr("Export Subs")
                            onClicked: {
                                clipboardHelper.copyText(subscriptionsModel.exportJson())
                                page.exportNotice = qsTr("Subscriptions copied to clipboard.")
                                exportTimer.restart()
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.exportNotice.length > 0
                        text: page.exportNotice
                        color: "#2e7d32"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            GroupBox {
                title: qsTr("Maintenance")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: ytdlpUpdater.statusText.length > 0 ? ytdlpUpdater.statusText : qsTr("yt-dlp status unknown.")
                        color: effectiveTheme.textColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            text: ytdlpUpdater.checking ? qsTr("Checking…") : qsTr("Check for updates")
                            enabled: !ytdlpUpdater.checking && !ytdlpUpdater.updating
                            onClicked: ytdlpUpdater.checkForUpdates()
                        }
                        Button {
                            text: ytdlpUpdater.updating ? qsTr("Updating…") : qsTr("Update yt-dlp")
                            enabled: !ytdlpUpdater.updating
                            onClicked: ytdlpUpdater.updateYtdlp()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: dependencyChecker.missingMessage.length > 0
                        text: dependencyChecker.missingMessage
                        color: "#d32f2f"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: dependencyChecker.missingMessage.length === 0
                        text: qsTr("All dependencies (yt-dlp, ffmpeg) found.")
                        color: "#2e7d32"
                        font.pixelSize: 12
                    }
                }
            }

            GroupBox {
                title: qsTr("About")
                Layout.fillWidth: true
                palette.text: effectiveTheme.textColor
                Label {
                    anchors.fill: parent
                    text: qsTr("PrimoTube — a Qt/QML YouTube client with libmpv playback, yt-dlp downloads, and local history, favorites, and subscriptions.")
                    color: effectiveTheme.textColor
                    opacity: 0.7
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose download folder")
        currentFolder: settingsManager.localFileToUrl(settingsManager.downloadDirectory)
        onAccepted: page.applyDir(page.folderUrlToPath(selectedFolder))
    }

    Timer {
        id: qualityTimer
        interval: 6000
        onTriggered: page.qualityNotice = ""
    }

    Timer {
        id: exportTimer
        interval: 6000
        onTriggered: page.exportNotice = ""
    }
}
