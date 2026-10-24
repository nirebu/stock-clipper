import QtQuick
import StockClipper 1.0

Sheet {
    id: dlg

    title: "Settings"
    signal browseRequested(bool forRoot)

    property string rootText: ""
    property string outText: ""
    property string suffixText: ""
    property string formatValue: "h264"
    property int crfValue: 18
    property string presetValue: "medium"
    property string minDurText: "5"
    property bool audioDefault: false

    onOpenChanged: if (open) load()

    function load() {
        rootText = App.root
        outText = App.outputDir
        suffixText = App.suffix
        formatValue = App.format
        crfValue = App.crf
        presetValue = App.preset
        minDurText = String(App.minDuration)
        audioDefault = App.audioDefault
    }

    function apply() {
        App.saveSettings({
            "root": rootText,
            "outputDir": outText,
            "suffix": suffixText,
            "format": formatValue,
            "crf": crfValue,
            "preset": presetValue,
            "minDuration": Number(minDurText) || 0,
            "audio": audioDefault
        })
    }

    function setFolder(path, forRoot) {
        if (forRoot)
            rootText = path
        else
            outText = path
    }

    Text { text: "Source folder"; color: Theme.muted; font.pixelSize: 12 }

    Row {
        width: parent.width
        spacing: 10
        Rectangle {
            width: parent.width - 96
            height: 30
            radius: 6
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            TextInput {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                text: dlg.rootText
                color: Theme.text
                font.pixelSize: 12
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                onEditingFinished: dlg.rootText = text
            }
        }
        Btn {
            label: "Browse…"
            small: true
            ghost: true
            onClicked: dlg.browseRequested(true)
        }
    }

    Text { text: "Output folder"; color: Theme.muted; font.pixelSize: 12 }

    Row {
        width: parent.width
        spacing: 10
        Rectangle {
            width: parent.width - 96
            height: 30
            radius: 6
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            TextInput {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                text: dlg.outText
                color: Theme.text
                font.pixelSize: 12
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                onEditingFinished: dlg.outText = text
            }
        }
        Btn {
            label: "Browse…"
            small: true
            ghost: true
            onClicked: dlg.browseRequested(false)
        }
    }

    Row {
        width: parent.width
        spacing: 10
        Text {
            width: 150
            text: "Format"
            color: Theme.muted
            font.pixelSize: 12
        }
        Row {
            spacing: 6
            Repeater {
                model: [
                    { id: "h264", label: "H.264" },
                    { id: "prores", label: "ProRes 422 HQ" },
                    { id: "dnxhr", label: "DNxHR HQX" }
                ]
                delegate: Btn {
                    required property var modelData
                    label: modelData.label
                    tiny: true
                    on: dlg.formatValue === modelData.id
                    onClicked: dlg.formatValue = modelData.id
                }
            }
        }
    }

    Text {
        visible: dlg.formatValue !== "h264"
        width: parent.width
        wrapMode: Text.WordWrap
        text: dlg.formatValue === "prores"
              ? "ProRes 422 HQ · 10-bit 4:2:2 · .mov — much larger files, no quality knob."
              : "DNxHR HQX · 10-bit 4:2:2 · .mov — much larger files, no quality knob."
        color: Theme.muted
        font.pixelSize: 11
    }

    Row {
        width: parent.width
        spacing: 10
        Text {
            width: 150
            text: "Filename suffix"
            color: Theme.muted
            font.pixelSize: 12
        }
        Rectangle {
            width: 130
            height: 30
            radius: 6
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            TextInput {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                text: dlg.suffixText
                color: Theme.text
                font.pixelSize: 12
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                onEditingFinished: dlg.suffixText = text
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "clip" + dlg.suffixText + (dlg.formatValue === "h264" ? ".mp4" : ".mov")
            color: Theme.muted
            font.pixelSize: 11
            font.family: Theme.monoFont
        }
    }

    Row {
        width: parent.width
        spacing: 10
        visible: dlg.formatValue === "h264"
        Text {
            width: 150
            text: "Quality (CRF " + dlg.crfValue + ")"
            color: Theme.muted
            font.pixelSize: 12
        }

        Item {
            id: crfHost
            width: 210
            height: 22

            Rectangle {
                id: rail
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: Theme.panel3
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: (dlg.crfValue - 12) / 18 * rail.width
                height: 4
                radius: 2
                color: Theme.accent
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: (dlg.crfValue - 12) / 18 * (rail.width - 12)
                width: 12
                height: 12
                radius: 6
                color: Theme.accent
            }
            MouseArea {
                anchors.fill: parent
                function setFrom(x) {
                    dlg.crfValue = Math.round(12 + Math.max(0, Math.min(1, x / rail.width)) * 18)
                }
                onPressed: (mouse) => setFrom(mouse.x)
                onPositionChanged: (mouse) => { if (pressed) setFrom(mouse.x) }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "lower = better & bigger"
            color: Theme.muted
            font.pixelSize: 11
        }
    }

    Row {
        width: parent.width
        spacing: 10
        visible: dlg.formatValue === "h264"
        Text {
            width: 150
            text: "Encoding preset"
            color: Theme.muted
            font.pixelSize: 12
        }
        Row {
            spacing: 6
            Repeater {
                model: ["veryfast", "fast", "medium", "slow", "slower"]
                delegate: Btn {
                    required property string modelData
                    label: modelData
                    tiny: true
                    on: dlg.presetValue === modelData
                    onClicked: dlg.presetValue = modelData
                }
            }
        }
    }

    Row {
        width: parent.width
        spacing: 10
        Text {
            width: 150
            text: "Warn below (seconds)"
            color: Theme.muted
            font.pixelSize: 12
        }
        Rectangle {
            width: 90
            height: 30
            radius: 6
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            TextInput {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                text: dlg.minDurText
                color: Theme.text
                font.pixelSize: 12
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                onEditingFinished: dlg.minDurText = text
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "short-clip hint"
            color: Theme.muted
            font.pixelSize: 11
        }
    }

    Row {
        width: parent.width
        spacing: 10
        Text {
            width: 150
            text: "Audio"
            color: Theme.muted
            font.pixelSize: 12
        }
        TickBox {
            anchors.verticalCenter: parent.verticalCenter
            label: "Keep audio by default"
            checked: dlg.audioDefault
            onToggled: (checked) => dlg.audioDefault = checked
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: "Silent unless ticked. Kept audio is copied when the container accepts it, "
              + "otherwise re-encoded to AAC. H.264 renders are MP4 (yuv420p, faststart); "
              + "ProRes and DNxHR render 10-bit 4:2:2 MOV. Native resolution and frame rate."
        color: Theme.muted
        font.pixelSize: 11
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.lineSoft
    }

    Row {
        width: parent.width
        spacing: 10

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 210
            text: App.ffmpegOk ? "ffmpeg ready" : "ffmpeg / ffprobe not found in PATH"
            color: App.ffmpegOk ? Theme.muted : Theme.danger
            font.pixelSize: 11
        }
        Btn {
            label: "Cancel"
            ghost: true
            onClicked: dlg.closed()
        }
        Btn {
            label: "Save"
            primary: true
            onClicked: {
                dlg.apply()
                dlg.closed()
            }
        }
    }
}
