// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProfileBar.qml -- device-scoped profile switcher (Workstream D).
//
// A compact bar above the editor tabs that lets the user switch between the
// profiles for the currently-selected device and create / rename / duplicate /
// delete them. Profiles are device-scoped: selecting a device activates that
// device's profile (creating a "Default" the first time) via
// ProfileController.activateDeviceProfile().
//
// All state lives in ProfileController (the QML singleton); this component is a
// thin view that re-queries profilesForDevice() on profilesChanged and keeps
// the dropdown selection in sync with activeProfileId on profileChanged.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AjazzControlCenter

RowLayout {
    id: root

    // Machine codename of the device being edited (from ProfileEditor).
    property string deviceCodename: ""

    // {id, name} maps for this device; rebuilt from the controller.
    property var _profiles: []

    readonly property int materialTheme:
        ThemeService.effectiveMode === "light" ? Material.Light : Material.Dark

    spacing: Theme.spacingSm

    function _refresh() {
        root._profiles = ProfileController.profilesForDevice(root.deviceCodename);
        _syncCurrentIndex();
    }

    function _syncCurrentIndex() {
        combo.currentIndex = combo.indexOfValue(ProfileController.activeProfileId());
    }

    // Activate this device's profile whenever the selected device changes.
    onDeviceCodenameChanged: {
        if (root.deviceCodename !== "") {
            ProfileController.activateDeviceProfile(root.deviceCodename);
        }
        root._refresh();
    }
    Component.onCompleted: {
        if (root.deviceCodename !== "") {
            ProfileController.activateDeviceProfile(root.deviceCodename);
        }
        root._refresh();
    }

    Connections {
        target: ProfileController
        function onProfilesChanged() { root._refresh(); }
        function onProfileChanged() { root._syncCurrentIndex(); }
    }

    Label {
        text: qsTr("Profile")
        color: Theme.fgFaint
        Layout.alignment: Qt.AlignVCenter
    }

    ComboBox {
        id: combo
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        model: root._profiles
        textRole: "name"
        valueRole: "id"
        Material.theme: root.materialTheme
        Material.accent: Theme.accent

        // onActivated only fires on user selection (not programmatic
        // currentIndex changes), so switching here never recurses.
        onActivated: {
            var id = combo.currentValue;
            if (id && id !== ProfileController.activeProfileId()) {
                ProfileController.loadProfileById(id);
            }
        }

        Accessible.role: Accessible.ComboBox
        Accessible.name: qsTr("Active profile")
    }

    // ---- Actions: New / Rename / Duplicate / Delete -----------------------
    component BarButton: ToolButton {
        required property string glyph
        required property string tip
        implicitWidth: 36
        implicitHeight: 36
        contentItem: Text {
            anchors.centerIn: parent
            font.family: "Material Symbols Outlined"
            font.pixelSize: 20
            text: parent.glyph
            color: parent.enabled ? Theme.fgPrimary : Theme.fgFaint
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        ToolTip.visible: hovered
        ToolTip.text: tip
        Accessible.name: tip
    }

    BarButton {
        glyph: "add"
        tip: qsTr("New profile")
        onClicked: nameDialog.openFor("new")
    }
    BarButton {
        glyph: "edit"
        tip: qsTr("Rename profile")
        enabled: ProfileController.activeProfileId() !== ""
        onClicked: nameDialog.openFor("rename")
    }
    BarButton {
        glyph: "content_copy"
        tip: qsTr("Duplicate profile")
        enabled: ProfileController.activeProfileId() !== ""
        onClicked: nameDialog.openFor("duplicate")
    }
    BarButton {
        glyph: "delete"
        tip: qsTr("Delete profile")
        enabled: ProfileController.activeProfileId() !== ""
        onClicked: confirmDelete.open()
    }

    // ---- Name input dialog (new / rename / duplicate) ---------------------
    Dialog {
        id: nameDialog
        title: _mode === "new" ? qsTr("New profile")
             : (_mode === "rename" ? qsTr("Rename profile") : qsTr("Duplicate profile"))
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel

        Material.theme: root.materialTheme
        Material.accent: Theme.accent

        property string _mode: "new"

        function openFor(mode) {
            _mode = mode;
            if (mode === "rename") {
                nameField.text = ProfileController.activeProfileName();
            } else if (mode === "duplicate") {
                nameField.text = ProfileController.activeProfileName() + qsTr(" copy");
            } else {
                nameField.text = qsTr("New profile");
            }
            open();
            nameField.selectAll();
            nameField.forceActiveFocus();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSm
            Label {
                text: qsTr("Profile name")
                color: Theme.fgFaint
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                Material.theme: root.materialTheme
                Material.accent: Theme.accent
                onAccepted: nameDialog.accept()
            }
        }

        onAccepted: {
            var name = nameField.text.trim();
            if (name === "") return;
            if (_mode === "new") {
                ProfileController.createProfile(name, root.deviceCodename);
            } else if (_mode === "rename") {
                ProfileController.renameActiveProfile(name);
            } else {
                ProfileController.duplicateProfile("", name);
            }
        }
    }

    // ---- Delete confirmation ----------------------------------------------
    Dialog {
        id: confirmDelete
        title: qsTr("Delete profile?")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 360
        standardButtons: Dialog.Yes | Dialog.No

        Material.theme: root.materialTheme
        Material.accent: Theme.accent

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.fgPrimary
            text: qsTr("Delete \"%1\"? This cannot be undone.")
                .arg(ProfileController.activeProfileName())
        }

        onAccepted: ProfileController.deleteProfile(ProfileController.activeProfileId())
    }
}
