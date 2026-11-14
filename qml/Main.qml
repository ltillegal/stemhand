import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Stemhand 1.0

ApplicationWindow {
    id: window

    width: 1200
    height: 760
    minimumWidth: 920
    minimumHeight: 560
    visible: true
    color: pal.bg
    title: audio.hasAudio ? audio.fileName + " — Stemhand" : "Stemhand"

    readonly property var pal: ({
        bg: "#0a0b0e",
        panel: "#111319",
        raised: "#171a22",
        line: "#232733",
        text: "#e9ecf2",
        dim: "#828b9e",
        accent: "#7dd3fc",
        ok: "#6ee7b7",
        warn: "#fbbf24",
        bad: "#f87171"
    })

    Material.theme: Material.Dark
    Material.accent: pal.accent
    Material.background: pal.bg
    Material.foreground: pal.text

    function formatTime(seconds) {
        if (!seconds || seconds <= 0)
            return "--:--";
        const minutes = Math.floor(seconds / 60);
        const rest = Math.floor(seconds % 60);
        return minutes + ":" + (rest < 10 ? "0" : "") + rest;
    }

    Shortcut { sequence: "O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "R"; onActivated: store.rescan() }
    Shortcut { sequence: "I"; onActivated: importDialog.open() }
    Shortcut { sequence: "D"; onActivated: denoise.run(audio.path) }
    Shortcut { sequence: "Q"; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }

    FileDialog {
        id: openDialog
        title: qsTr("Open audio or video")
        nameFilters: [
            qsTr("Audio and video (*.wav *.flac *.mp3 *.aif *.aiff *.m4a *.ogg *.opus *.mp4 *.mkv *.mov)"),
            qsTr("All files (*)")
        ]
        onAccepted: audio.load(selectedFile)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a model folder")
        fileMode: FileDialog.Directory
        onAccepted: store.importFolder(selectedFolder)
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0)
                audio.load(drop.urls[0]);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: pal.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 18
                spacing: 10

                Rectangle {
                    width: 9
                    height: 9
                    radius: 2
                    color: pal.accent
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: qsTr("STEMHAND")
                    color: pal.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2.4
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "v" + appVersion
                    color: pal.dim
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignBottom
                    Layout.bottomMargin: 2
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Separate 4 stems")
                    flat: true
                    visible: audio.hasAudio
                    enabled: separate.hasStemModel && !separate.busy && !denoise.busy
                    onClicked: separate.run(audio.path)
                }

                Button {
                    text: qsTr("Denoise voice")
                    flat: true
                    visible: audio.hasAudio
                    enabled: denoise.hasVoiceModel && !denoise.busy && !audio.loading
                    onClicked: denoise.run(audio.path)
                }

                Button {
                    text: qsTr("Open audio")
                    highlighted: true
                    onClicked: openDialog.open()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: pal.line
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Models
            Rectangle {
                Layout.preferredWidth: 356
                Layout.fillHeight: true
                color: pal.panel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("MODELS")
                            color: pal.dim
                            font.pixelSize: 10
                            font.letterSpacing: 1.8
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: store.models.length
                            color: pal.dim
                            font.pixelSize: 10
                        }
                    }

                    ListView {
                        id: modelList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: store.models
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: ModelRow {
                            width: modelList.width
                            entry: modelData
                            colors: window.pal
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            visible: store.models.length === 0
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("No models yet.\nPress I to import a folder with manifest.json.")
                            color: pal.dim
                            font.pixelSize: 12
                            lineHeight: 1.4
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: pal.line
                    }

                    Text {
                        Layout.fillWidth: true
                        text: store.summary
                        color: pal.dim
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: store.root
                        color: pal.dim
                        font.pixelSize: 9
                        opacity: 0.7
                        elide: Text.ElideMiddle
                        ToolTip.visible: rootHover.hovered
                        ToolTip.text: store.root
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Button {
                            Layout.fillWidth: true
                            text: updates.busy ? qsTr("Checking…") : qsTr("Update models")
                            flat: true
                            enabled: !updates.busy
                            onClicked: updates.checkAndInstall()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Button {
                                text: qsTr("Rescan")
                                flat: true
                                onClicked: store.rescan()
                            }
                            Button {
                                text: qsTr("Import")
                                flat: true
                                onClicked: importDialog.open()
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Folder")
                                flat: true
                                onClicked: store.openRootInFileManager()
                            }
                        }
                    }
                }

                HoverHandler { id: rootHover }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: pal.line
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        spacing: 16

                        ColumnLayout {
                            spacing: 2

                            Text {
                                text: audio.hasAudio ? audio.fileName : qsTr("Nothing open")
                                color: pal.text
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.maximumWidth: 520
                            }

                            Text {
                                text: audio.hasAudio ? audio.formatLabel
                                                     : qsTr("Open a file to see its waveform")
                                color: pal.dim
                                font.pixelSize: 11
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: audio.duration > 0
                            text: window.formatTime(audio.duration)
                            color: pal.text
                            font.pixelSize: 13
                            font.family: "monospace"
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.bottomMargin: 8

                    Wave {
                        anchors.fill: parent
                        peaks: audio.peaks
                        accent: pal.accent
                        gridColor: "#1b1f27"
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !audio.hasAudio && !audio.loading
                        text: qsTr("Drop a file here, or press O")
                        color: pal.dim
                        font.pixelSize: 13
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 28
                        height: 28
                        visible: audio.loading
                        running: audio.loading
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 40 : 0
                    visible: separate.stems.length > 0
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        spacing: 4

                        Text {
                            text: qsTr("STEMS")
                            color: pal.dim
                            font.pixelSize: 9
                            font.letterSpacing: 1.6
                        }

                        Repeater {
                            model: separate.stems
                            Button {
                                text: modelData.name
                                flat: true
                                font.pixelSize: 11
                                onClicked: audio.load(modelData.url)
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Stems folder")
                            flat: true
                            font.pixelSize: 11
                            onClicked: separate.openStemsFolder()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 26 : 0
                    visible: (denoise.message.length > 0 || updates.message.length > 0
                              || separate.message.length > 0)
                             && denoise.error.length === 0 && updates.error.length === 0
                             && separate.error.length === 0
                    color: (denoise.busy || updates.busy || separate.busy) ? "#1b1a10" : "#0f1a17"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        verticalAlignment: Text.AlignVCenter
                        text: denoise.message.length > 0 ? denoise.message
                              : (separate.message.length > 0 ? separate.message : updates.message)
                        color: (denoise.busy || updates.busy || separate.busy) ? pal.warn : pal.ok
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 30 : 0
                    visible: denoise.error.length > 0 || updates.error.length > 0
                              || separate.error.length > 0 || audio.error.length > 0
                              || store.lastError.length > 0
                    color: "#2a1416"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        verticalAlignment: Text.AlignVCenter
                        text: denoise.error.length > 0 ? denoise.error
                              : (separate.error.length > 0 ? separate.error
                              : (updates.error.length > 0 ? updates.error
                              : (audio.error.length > 0 ? audio.error : store.lastError)))
                        color: pal.bad
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: pal.panel

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26

                        Text {
                            text: audio.hasAudio ? audio.codec : qsTr("ready")
                            color: pal.dim
                            font.pixelSize: 10
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("O open · D denoise · R rescan · I import · Q quit")
                            color: pal.dim
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }
}
