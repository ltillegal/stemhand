import QtQuick
import Photone 1.0

Item {
    id: root

    property var peaks: []
    property color accent: "#7dd3fc"
    property color gridColor: "#1b1f27"

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: "#0e1116"
        border.width: 1
        border.color: root.gridColor
    }

    WaveformItem {
        anchors.fill: parent
        anchors.margins: 16
        peaks: root.peaks
        accent: root.accent
        gridColor: root.gridColor
    }
}
