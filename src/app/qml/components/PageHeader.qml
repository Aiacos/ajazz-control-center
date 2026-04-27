// SPDX-License-Identifier: GPL-3.0-or-later
//
// PageHeader.qml — title + optional subtitle row used at the top of pages.
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter

ColumnLayout {
    id: root
    spacing: 2

    property string title: ""
    property string subtitle: ""

    Text {
        text: root.title
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontXl
        Layout.fillWidth: true
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }
    Text {
        visible: root.subtitle.length > 0
        text: root.subtitle
        color: Theme.fgMuted
        font.pixelSize: Theme.fontSm
        Layout.fillWidth: true
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
    }
}
