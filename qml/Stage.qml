import QtQuick
import QtMultimedia
import StockClipper 1.0

Item {
    id: stage

    readonly property bool turned: App.rotate === 90 || App.rotate === 270
    // eff* is the bounding box after rotation, used for fitting only.
    // The item itself keeps the frame's own aspect so Stretch cannot distort it.
    readonly property real dw: App.curDisplayW
    readonly property real dh: App.curDisplayH
    readonly property real effW: turned ? dh : dw
    readonly property real effH: turned ? dw : dh
    readonly property real itemW: App.viewingRender ? effW : dw
    readonly property real itemH: App.viewingRender ? effH : dh
    readonly property real fit: (effW > 0 && effH > 0)
                                ? Math.min(Math.max(1, width - 36) / effW,
                                           Math.max(1, height - 36) / effH)
                                : 1

    Rectangle {
        anchors.fill: parent
        color: "#07090c"
        radius: Theme.radius
        border.width: 1
        border.color: Theme.lineSoft
    }

    // flips are applied after rotation: flip wrapper outside, rotation inside
    Item {
        id: flipWrap
        anchors.centerIn: parent
        width: stage.itemW * stage.fit
        height: stage.itemH * stage.fit
        visible: App.hasSelection && App.curError === ""

        transform: Scale {
            origin.x: flipWrap.width / 2
            origin.y: flipWrap.height / 2
            xScale: App.flipH ? -1 : 1
            yScale: App.flipV ? -1 : 1
        }

        Item {
            anchors.fill: parent
            rotation: App.viewingRender ? 0 : App.rotate

            VideoOutput {
                id: videoOut
                anchors.fill: parent
                fillMode: VideoOutput.Stretch
                Component.onCompleted: App.attachVideoSink(videoOut.videoSink)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: App.togglePlay()
        acceptedButtons: Qt.LeftButton
    }

    // big play overlay, only while paused
    Rectangle {
        anchors.centerIn: parent
        width: 68
        height: 68
        radius: 34
        color: "#a8000000"
        border.width: 1
        border.color: "#14ffffff"
        visible: App.hasSelection && !App.playing && App.curError === ""

        Text {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 3
            text: "▶"
            color: "#d8ffffff"
            font.pixelSize: 26
        }
    }

    // empty / error states
    Column {
        anchors.centerIn: parent
        spacing: 8
        visible: !App.hasSelection || App.curError !== ""

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !App.hasSelection ? "No clip selected" : "Cannot read this clip"
            color: App.curError === "" ? Theme.muted : Theme.danger
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(520, stage.width - 60)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: !App.hasSelection
                  ? "Pick a clip in the library to trim, rotate and render it."
                  : App.curError
            color: Theme.muted
            font.pixelSize: 12
        }
    }

    // hud
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        width: hud.implicitWidth + 18
        height: hud.implicitHeight + 12
        radius: 7
        color: "#b006080b"
        border.width: 1
        border.color: "#10ffffff"
        visible: App.hasSelection

        Column {
            id: hud
            anchors.centerIn: parent
            spacing: 2

            Text {
                text: App.curName
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, stage.width - 250)
            }
            Text {
                text: App.hudMeta
                color: Theme.muted
                font.pixelSize: 11
                font.family: Theme.monoFont
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        height: 22
        width: renderedRow.implicitWidth + 16
        radius: 5
        visible: App.viewingRender
        color: "#1b2230"
        border.width: 1
        border.color: Theme.line

        Row {
            id: renderedRow
            anchors.centerIn: parent
            spacing: 8
            Text {
                text: "▶ " + App.renderedName
                color: Theme.ok
                font.pixelSize: 11
                font.family: Theme.monoFont
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, 260)
            }
            Text {
                text: "back to source"
                color: Theme.muted
                font.pixelSize: 11
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.backToSource()
                }
            }
        }
    }
}
