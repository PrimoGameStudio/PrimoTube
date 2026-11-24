import QtQuick
import QtTest

// QML-side regression tests for the reactive bindings and gates that were
// broken before the count/NOTIFY and URL-allow-list fixes.
Item {
    id: root
    width: 200
    height: 200

    // Same binding pattern as PlayerPage.qml's Favorite button.
    property string favoriteLabel: {
        favoritesModel.count
        return favoritesModel.contains("qmlId") ? "★ Favorited" : "☆ Favorite"
    }

    // Same binding pattern as ChannelPage.qml's Subscribe button.
    property string subscribeLabel: {
        subscriptionsModel.count
        return subscriptionsModel.contains("UC1234567890123456789012")
            ? "★ Subscribed" : "☆ Subscribe"
    }

    // Same binding pattern as SearchPage.qml's count labels.
    property string countLabel: "Subscriptions: " + subscriptionsModel.count

    property var requestedItem: null

    Connections {
        target: queueModel
        function onItemRequested(videoId, title, channel, channelId, channelUrl,
                                watchUrl, durationString, duration, thumbnail, description) {
            root.requestedItem = {
                videoId: videoId,
                title: title,
                thumbnail: thumbnail
            }
        }
    }

    TestCase {
        name: "ReactiveBindings"
        when: windowShown

        function test_favoriteLabelUpdates() {
            compare(root.favoriteLabel, "☆ Favorite")
            favoritesModel.addItem("qmlId", "QML Video", "", "", "", "", 0)
            tryCompare(root, "favoriteLabel", "★ Favorited")
            favoritesModel.removeItem("qmlId")
            tryCompare(root, "favoriteLabel", "☆ Favorite")
        }

        function test_subscribeLabelUpdates() {
            compare(root.subscribeLabel, "☆ Subscribe")
            subscriptionsModel.subscribe("UC1234567890123456789012", "Channel")
            tryCompare(root, "subscribeLabel", "★ Subscribed")
            compare(root.countLabel, "Subscriptions: 1")
            subscriptionsModel.unsubscribe("UC1234567890123456789012")
            tryCompare(root, "subscribeLabel", "☆ Subscribe")
            compare(root.countLabel, "Subscriptions: 0")
        }

        function test_queueItemRequestedReachesQml() {
            queueModel.clear()
            queueModel.enqueue("q1", "Queued One", "Chan", "thumb1", "url", "1:00", 60)
            queueModel.enqueue("q2", "Queued Two", "Chan", "thumb2", "url", "2:00", 120)
            verify(root.requestedItem === null)
            queueModel.playIndex(1)
            verify(root.requestedItem !== null)
            compare(root.requestedItem.videoId, "q2")
            compare(root.requestedItem.title, "Queued Two")
            compare(root.requestedItem.thumbnail, "thumb2")
            queueModel.clear()
        }

        function test_urlGate() {
            verify(clipboardHelper.isAllowedPlaybackTarget("dQw4w9WgXcQ"))
            verify(clipboardHelper.isAllowedPlaybackTarget(
                       "https://www.youtube.com/watch?v=dQw4w9WgXcQ"))
            verify(!clipboardHelper.isAllowedPlaybackTarget("file:///etc/passwd"))
            verify(!clipboardHelper.isAllowedPlaybackTarget("https://evil.com/video"))
            verify(!clipboardHelper.isAllowedPlaybackTarget("javascript:alert(1)"))
        }
    }
}
