// SPDX-License-Identifier: GPL-3.0-or-later
//
// Notification.qml — compact transient notifications stacked at the top-right.
//
// Each show() appends a pill that slides in below the previous one; pills do
// NOT overwrite each other. Each dwells briefly then fades out, and the ones
// below slide up to fill the gap (Column `move` transition).
//
// Usage:
//     Notification { id: notes }
//     notes.show("Time synced")                       // success green (default)
//     notes.show("Sync failed", Theme.errorAccent)    // custom colour
//
// `pragma ComponentBehavior: Bound` makes the outer `root` id statically
// resolvable inside the Repeater delegate (no unqualified-access warnings).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import AjazzControlCenter

Item {
    id: root
    z: 1000
    anchors.fill: parent // transparent overlay; only the pills paint + take input

    /// Dwell time before a pill begins fading out.
    property int dwellMs: 3000

    property int _nextUid: 0

    /// Append a notification with the given message and (optional) accent colour.
    function show(message, color) {
        stackModel.append({
            uid: root._nextUid++,
            message: message,
            accent: (color || Theme.successAccent).toString()
        });
    }

    function _removeUid(uid) {
        for (var i = 0; i < stackModel.count; ++i) {
            if (stackModel.get(i).uid === uid) {
                stackModel.remove(i);
                return;
            }
        }
    }

    ListModel { id: stackModel }

    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingSm

        // New pills fade + slide in; survivors slide up when one above is removed.
        add: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: Theme.durationMedium; easing.type: Theme.easingStandard
            }
        }
        move: Transition {
            NumberAnimation {
                properties: "y"
                duration: Theme.durationMedium; easing.type: Theme.easingStandard
            }
        }

        Repeater {
            model: stackModel

            delegate: Item {
                id: note
                required property int uid
                required property string message
                required property string accent

                width: pill.width
                height: pill.height
                // anchor to the right edge of the Column so different-width pills
                // stay flush-right under each other.
                anchors.right: parent ? parent.right : undefined

                Rectangle {
                    id: pill
                    radius: Theme.radiusMd
                    color: note.accent
                    width: label.implicitWidth + Theme.spacingMd * 2
                    height: 32
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: Theme.elevationShadowColor
                        shadowVerticalOffset: Theme.elevation3.offsetY
                        shadowBlur: Theme.elevation3.blur
                        shadowOpacity: Theme.elevation3.opacity
                    }

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: note.message
                        color: "#0e1011" // dark text reads best on the status accents
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.Medium
                    }
                }

                // Fade out after the dwell, then drop from the model (which
                // triggers the Column `move` reflow of the pills below).
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easingStandard }
                }
                Timer {
                    id: dwell
                    interval: root.dwellMs
                    running: true
                    onTriggered: { note.opacity = 0; reap.start(); }
                }
                Timer {
                    id: reap
                    interval: Theme.durationMedium
                    onTriggered: root._removeUid(note.uid)
                }
            }
        }
    }
}
