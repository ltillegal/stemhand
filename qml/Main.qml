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
    Shortcut { sequence: "D"; onActivated: denoise.run(audio.url) }
    Shortcut { sequence: "Space"; onActivated: player.toggle() }
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

    FileDialog {
        id: exportFileDialog
        title: qsTr("Export the opened audio")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV (*.wav)"), qsTr("FLAC (*.flac)"), qsTr("MP3 (*.mp3)"),
                      qsTr("All files (*)")]
        currentFile: exporter.suggestedName.length > 0 ? "file://" + exporter.suggestedName : ""
        onAccepted: exporter.exportFile(selectedFile)
    }

    FileDialog {
        id: batchDialog
        title: qsTr("Add files to the batch")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Audio and video (*.wav *.flac *.mp3 *.aif *.aiff *.m4a *.ogg *.opus *.mp4 *.mkv *.mov)"),
            qsTr("All files (*)")
        ]
        onAccepted: batch.addFiles(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export the mix")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        currentFile: separate.stems.length > 0 ? "file://" + separate.suggestedMixName() : ""
        onAccepted: separate.exportMix(selectedFile)
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
                    id: separateButton
                    text: separate.busy ? qsTr("Cancel") : qsTr("Separate 4 stems")
                    flat: true
                    visible: audio.hasAudio
                    enabled: separate.hasStemModel && !denoise.busy
                    onClicked: separate.busy ? separate.cancel() : separate.run(audio.url)

                    HoverHandler { id: separateHover }

                    ToolTip.visible: separateHover.hovered && !separateButton.enabled
                    ToolTip.text: !separate.hasStemModel
                                  ? qsTr("No stem model installed — run scripts/install-demucs.sh")
                                  : qsTr("Busy with another job")
                }

                Button {
                    id: denoiseButton
                    text: denoise.busy ? qsTr("Cancel") : qsTr("Denoise voice")
                    flat: true
                    visible: audio.hasAudio
                    enabled: denoise.busy || (denoise.hasVoiceModel && !audio.loading)
                    onClicked: denoise.busy ? denoise.cancel() : denoise.run(audio.url)

                    HoverHandler { id: denoiseHover }

                    ToolTip.visible: denoiseHover.hovered && !denoiseButton.enabled
                    ToolTip.text: !denoise.hasVoiceModel
                                  ? qsTr("No voice model installed — run scripts/install-gtcrn.sh")
                                  : qsTr("Waiting for the audio to load")
                }

                Button {
                    text: qsTr("Export…")
                    flat: true
                    visible: audio.hasAudio
                    enabled: !exporter.busy
                    onClicked: exportFileDialog.open()
                }

                Button {
                    text: qsTr("Batch…")
                    flat: true
                    onClicked: batchDialog.open()
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
                            text: qsTr("No models yet.\n\nFrom a terminal:\n"
                                      + "./scripts/install-gtcrn.sh gtcrn_simple.onnx\n"
                                      + "./scripts/install-demucs.sh htdemucs.onnx\n\n"
                                      + "or press I to import a folder with manifest.json.")
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
                            id: updateButton
                            Layout.fillWidth: true
                            text: updates.busy ? qsTr("Checking…") : qsTr("Update models")
                            flat: true
                            enabled: updates.canCheck && !updates.busy
                            onClicked: updates.checkAndInstall()

                            HoverHandler { id: updateHover }

                            ToolTip.visible: updateHover.hovered && !updateButton.enabled
                            ToolTip.text: qsTr("No registry configured.\n"
                                               + "Run once: stemhand --update <index-url>")
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
                        progress: player.duration > 0 ? player.position / player.duration : 0
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: player.hasAudio
                        onClicked: (mouse) => player.seek(
                                       Math.max(0, Math.min(1, mouse.x / width)) * player.duration)
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
                    Layout.preferredHeight: visible ? 132 : 0
                    visible: batch.jobs.length > 0
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        anchors.topMargin: 6
                        anchors.bottomMargin: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("BATCH")
                                color: pal.dim
                                font.pixelSize: 9
                                font.letterSpacing: 1.6
                            }

                            Repeater {
                                model: [
                                    { key: "denoise", label: qsTr("Denoise") },
                                    { key: "separate", label: qsTr("Stems") }
                                ]

                                Rectangle {
                                    width: 62
                                    height: 18
                                    radius: 3
                                    color: batch.operation === modelData.key ? pal.accent : "transparent"
                                    border.width: 1
                                    border.color: batch.operation === modelData.key ? pal.accent : pal.line

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: batch.operation === modelData.key ? "#0a0b0e" : pal.dim
                                        font.pixelSize: 9
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: batch.operation = modelData.key
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("Start")
                                flat: true
                                font.pixelSize: 11
                                highlighted: true
                                enabled: !batch.busy && batch.hasModel
                                onClicked: batch.start()
                            }

                            Button {
                                text: qsTr("Folder")
                                flat: true
                                font.pixelSize: 11
                                onClicked: batch.openOutputFolder()
                            }

                            Button {
                                text: batch.busy ? qsTr("Cancel") : qsTr("Clear")
                                flat: true
                                font.pixelSize: 11
                                onClicked: batch.busy ? batch.cancel() : batch.clear()
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: batch.jobs
                            spacing: 3

                            delegate: RowLayout {
                                width: ListView.view.width
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: modelData.status === "failed" ? pal.bad : pal.text
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    text: modelData.status === "ok"
                                          ? (modelData.detail || "ok")
                                          : (modelData.status === "failed"
                                             ? (modelData.detail || "failed")
                                             : modelData.status)
                                    color: modelData.status === "ok" ? pal.ok
                                         : (modelData.status === "failed" ? pal.bad
                                         : (modelData.status === "running" ? pal.accent : pal.dim))
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 320
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 24 + separate.stems.length * 26 + 14 : 0
                    visible: separate.stems.length > 0
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        anchors.topMargin: 6
                        anchors.bottomMargin: 8
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: qsTr("STEMS")
                                color: pal.dim
                                font.pixelSize: 9
                                font.letterSpacing: 1.6
                            }

                            Button {
                                text: player.playing ? qsTr("Pause") : qsTr("Play")
                                flat: true
                                font.pixelSize: 11
                                enabled: player.hasAudio
                                onClicked: player.toggle()
                            }

                            Text {
                                text: player.positionText + " / " + player.durationText
                                color: pal.dim
                                font.pixelSize: 10
                                font.family: "monospace"
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("Export mix")
                                flat: true
                                font.pixelSize: 11
                                highlighted: true
                                enabled: !separate.busy && separate.stems.length > 0
                                onClicked: exportDialog.open()
                            }

                            Button {
                                text: qsTr("Folder")
                                flat: true
                                font.pixelSize: 11
                                onClicked: separate.openStemsFolder()
                            }
                        }

                        Repeater {
                            model: separate.stems

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: modelData.name
                                    color: pal.text
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 56
                                }

                                Rectangle {
                                    width: 20
                                    height: 18
                                    radius: 3
                                    color: modelData.muted ? pal.warn : "transparent"
                                    border.width: 1
                                    border.color: modelData.muted ? pal.warn : pal.line

                                    Text {
                                        anchors.centerIn: parent
                                        text: qsTr("M")
                                        color: modelData.muted ? "#0a0b0e" : pal.dim
                                        font.pixelSize: 9
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: separate.toggleMute(index)
                                    }
                                }

                                Rectangle {
                                    width: 20
                                    height: 18
                                    radius: 3
                                    color: modelData.solo ? pal.accent : "transparent"
                                    border.width: 1
                                    border.color: modelData.solo ? pal.accent : pal.line

                                    Text {
                                        anchors.centerIn: parent
                                        text: qsTr("S")
                                        color: modelData.solo ? "#0a0b0e" : pal.dim
                                        font.pixelSize: 9
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: separate.toggleSolo(index)
                                    }
                                }

                                Slider {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 18
                                    from: -24
                                    to: 12
                                    stepSize: 1
                                    value: modelData.gain
                                    onMoved: separate.setGain(index, value)
                                }

                                Text {
                                    text: (modelData.gain > 0 ? "+" : "") + modelData.gain.toFixed(0) + " dB"
                                    color: pal.dim
                                    font.pixelSize: 9
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 46
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 26 : 0
                    visible: denoise.message.length > 0 || updates.message.length > 0
                             || separate.message.length > 0 || batch.message.length > 0
                             || exporter.message.length > 0
                    color: (denoise.busy || updates.busy || separate.busy) ? "#1b1a10" : "#0f1a17"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        verticalAlignment: Text.AlignVCenter
                        text: denoise.message.length > 0 ? denoise.message
                              : (separate.message.length > 0 ? separate.message
                              : (batch.message.length > 0 ? batch.message
                              : (exporter.message.length > 0 ? exporter.message : updates.message)))
                        color: (denoise.busy || updates.busy || separate.busy || batch.busy
                                || exporter.busy) ? pal.warn : pal.ok
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 30 : 0
                    visible: denoise.error.length > 0 || updates.error.length > 0
                              || separate.error.length > 0 || batch.error.length > 0
                              || exporter.error.length > 0
                              || audio.error.length > 0 || store.lastError.length > 0
                    color: "#2a1416"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        verticalAlignment: Text.AlignVCenter
                        text: denoise.error.length > 0 ? denoise.error
                              : (separate.error.length > 0 ? separate.error
                              : (batch.error.length > 0 ? batch.error
                              : (exporter.error.length > 0 ? exporter.error
                              : (updates.error.length > 0 ? updates.error
                              : (audio.error.length > 0 ? audio.error : store.lastError)))))
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
