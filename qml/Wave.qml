import QtQuick
import Stemhand 1.0

Item {
    id: root

    property var peaks: []
    property color accent: "#7dd3fc"
    property color gridColor: "#1b1f27"
    property real progress: 0
    property color playhead: "#7dd3fc"

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

    Rectangle {
        visible: root.progress > 0
        x: 16 + (root.width - 32) * root.progress
        y: 12
        width: 1
        height: root.height - 24
        color: root.playhead
        opacity: 0.9
    }
}
