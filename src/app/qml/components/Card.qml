// SPDX-License-Identifier: GPL-3.0-or-later
//
// Card.qml — generic surface container.
//
// Uses Theme tokens so the card automatically follows the current branding /
// light-vs-dark mode. Children are placed in the default `data` slot; size is
// controlled by the parent via implicit/explicit width and height.
import QtQuick
import AjazzControlCenter

Rectangle {
    id: root
    color: Theme.tile
    border.color: Theme.borderSubtle
    border.width: 1
    radius: Theme.radiusLg
}
