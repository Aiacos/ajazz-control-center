// SPDX-License-Identifier: GPL-3.0-or-later
//
// EmptyState.qml — large centered placeholder shown when a panel has no
// content yet (e.g. no device selected, no profiles imported).
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter

ColumnLayout {
    id: root
    spacing: Theme.spacingMd

    property string title: ""
    property string body: ""

    Text {
        text: root.title
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontLg
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        text: root.body
        color: Theme.fgMuted
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 360
    }
}
