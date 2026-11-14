import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property var entry: ({})
    property var colors: ({})
    property bool hovered: false

    implicitHeight: layout.implicitHeight + 22
    radius: 8
    color: hovered ? "#1c202a" : colors.raised
    border.width: 1
    border.color: hovered ? "#2c3240" : colors.line

    function statusColor(status) {
        if (status === "ok")
            return root.colors.ok;
        if (status === "unverified")
            return root.colors.warn;
        return root.colors.bad;
    }

    component Chip: Rectangle {
        property string label: ""
        property color tint: root.colors.dim
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: 16
        radius: 3
        color: Qt.rgba(tint.r, tint.g, tint.b, 0.10)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: parent.tint
            font.pixelSize: 9
        }
    }

    HoverHandler {
        onHoveredChanged: root.hovered = hovered
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 11
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: root.statusColor(root.entry.status)
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                Layout.fillWidth: true
                text: root.entry.name || "?"
                color: root.colors.text
                font.pixelSize: 13
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Text {
                text: root.entry.version || ""
                color: root.colors.dim
                font.pixelSize: 10
            }
        }

        Text {
            Layout.fillWidth: true
            text: (root.entry.kindLabel || "") + " · " + (root.entry.sizeLabel || "")
            color: root.colors.dim
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Chip {
                label: root.entry.license || "unknown license"
                tint: root.entry.commercial ? root.colors.ok : root.colors.warn
            }

            Chip {
                label: root.entry.status
                tint: root.statusColor(root.entry.status)
            }

            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            visible: root.entry.status !== "ok" && root.entry.detail
            text: root.entry.detail || ""
            color: root.colors.dim
            font.pixelSize: 9
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
}
