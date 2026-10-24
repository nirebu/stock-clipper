import QtQuick
import StockClipper 1.0

Item {
    id: tl

    implicitHeight: 116
    opacity: App.hasSelection ? 1 : 0.45

    readonly property real duration: App.curDuration
    readonly property real inT: App.editIn
    readonly property real outT: App.editOut

    function xFor(t) {
        if (tl.duration <= 0)
            return 0
        return Math.max(0, Math.min(track.width, track.width * t / tl.duration))
    }

    function timeFor(x) {
        if (tl.duration <= 0)
            return 0
        return Math.max(0, Math.min(tl.duration, x / track.width * tl.duration))
    }

    /* ---------------- head: in / out / length + buttons ---------------- */

    Row {
        id: head
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        spacing: 14

        Column {
            spacing: 0
            Text { text: "IN"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
            TextInput {
                id: inField
                width: 68
                text: App.editInText
                color: Theme.text
                font.pixelSize: 13
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                enabled: App.hasSelection
                onEditingFinished: {
                    App.commitFieldText("in", text)
                    text = App.editInText
                    tl.forceActiveFocus()
                }
                Keys.onEscapePressed: { text = App.editInText; tl.forceActiveFocus() }
            }
        }

        Column {
            spacing: 0
            Text { text: "OUT"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
            TextInput {
                id: outField
                width: 68
                text: App.editOutText
                color: Theme.text
                font.pixelSize: 13
                font.family: Theme.monoFont
                selectByMouse: true
                selectionColor: Theme.accentDim
                enabled: App.hasSelection
                onEditingFinished: {
                    App.commitFieldText("out", text)
                    text = App.editOutText
                    tl.forceActiveFocus()
                }
                Keys.onEscapePressed: { text = App.editOutText; tl.forceActiveFocus() }
            }
        }

        Column {
            spacing: 0
            Text { text: "LENGTH"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
            Text {
                height: 18
                verticalAlignment: Text.AlignVCenter
                text: App.editLenText
                color: Theme.accent
                font.pixelSize: 13
                font.family: Theme.monoFont
            }
        }

        Item { width: 1; height: 1 }

        Btn {
            anchors.bottom: parent.bottom
            label: "Set in"
            tiny: true
            ghost: true
            active: App.hasSelection
            onClicked: App.setInAtPlayhead()
        }
        Btn {
            anchors.bottom: parent.bottom
            label: "Set out"
            tiny: true
            ghost: true
            active: App.hasSelection
            onClicked: App.setOutAtPlayhead()
        }
        Btn {
            anchors.bottom: parent.bottom
            label: "Reset"
            tiny: true
            ghost: true
            active: App.hasSelection
            onClicked: App.resetEdit()
        }

        Item { width: 12; height: 1 }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
            visible: App.shortWarn
            text: App.shortWarnText
            color: Theme.accent
            font.pixelSize: 11
        }
    }

    /* ---------------------------- track ------------------------------ */

    Item {
        id: trackArea
        anchors.left: parent.left
        anchors.right: parent.right
        y: 38
        height: 58

        Rectangle {
            id: track
            anchors.fill: parent
            radius: 6
            color: "#0a0c10"
            border.width: 1
            border.color: Theme.line

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: App.stripUrl
                fillMode: Image.Stretch
                asynchronous: true
                cache: false
                opacity: 0.92
                visible: App.stripUrl !== "" && status === Image.Ready
            }

            Rectangle {
                anchors.fill: parent
                visible: App.stripLoading
                radius: 6
                color: "#11161e"
                SequentialAnimation on opacity {
                    running: App.stripLoading
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.55; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 1.0; to: 0.55; duration: 800; easing.type: Easing.InOutQuad }
                }
            }

            // outside-the-cut shading
            Rectangle {
                x: 0
                y: 0
                height: parent.height
                width: tl.xFor(tl.inT)
                color: "#bf06080b"
                visible: App.hasSelection
            }
            Rectangle {
                x: tl.xFor(tl.outT)
                y: 0
                height: parent.height
                width: Math.max(0, parent.width - x)
                color: "#bf06080b"
                visible: App.hasSelection
            }

            Rectangle {
                x: tl.xFor(tl.inT) + 1
                y: 1
                height: parent.height - 2
                width: Math.max(0, tl.xFor(tl.outT) - tl.xFor(tl.inT) - 2)
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
                radius: 4
                visible: App.hasSelection
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => App.seekTo(tl.timeFor(mouse.x))
            }
        }

        // handles live outside the track rect so they are never clipped
        Item {
            id: handleIn
            x: tl.xFor(tl.inT) - width / 2
            y: 0
            width: 16
            height: trackArea.height
            visible: App.hasSelection

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 3
                height: parent.height
                radius: 2
                color: Theme.accent
            }
            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 28
                radius: 3
                color: Theme.accent
                border.width: 1
                border.color: "#40000000"
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                property bool dragging: false
                onPressed: dragging = true
                onReleased: dragging = false
                onPositionChanged: (mouse) => {
                    if (!dragging)
                        return
                    const px = mapToItem(track, mouse.x, 0).x
                    App.setIn(tl.timeFor(px))
                }
            }
        }

        Item {
            id: handleOut
            x: tl.xFor(tl.outT) - width / 2
            y: 0
            width: 16
            height: trackArea.height
            visible: App.hasSelection

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 3
                height: parent.height
                radius: 2
                color: Theme.accent
            }
            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 28
                radius: 3
                color: Theme.accent
                border.width: 1
                border.color: "#40000000"
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                property bool dragging: false
                onPressed: dragging = true
                onReleased: dragging = false
                onPositionChanged: (mouse) => {
                    if (!dragging)
                        return
                    const px = mapToItem(track, mouse.x, 0).x
                    App.setOut(tl.timeFor(px))
                }
            }
        }

        Rectangle {
            x: track.width * App.playheadFraction - 1
            y: -2
            width: 2
            height: trackArea.height + 4
            color: "#ffffff"
            visible: App.hasSelection
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: 7
                height: 7
                radius: 3.5
                color: "#ffffff"
            }
        }
    }

    /* ---------------------------- ruler ------------------------------ */

    Item {
        id: ruler
        anchors.left: parent.left
        anchors.right: parent.right
        y: 100
        height: 16

        Repeater {
            model: 5
            delegate: Item {
                required property int index
                x: track.width * index / 4
                width: 1
                height: 16

                Rectangle {
                    width: 1
                    height: 4
                    color: Theme.line
                }
                Text {
                    y: 4
                    x: index === 0 ? 0 : (index === 4 ? -implicitWidth : -implicitWidth / 2)
                    text: App.fmt(tl.duration * index / 4, 1)
                    color: Theme.muted
                    font.pixelSize: 9
                    font.family: Theme.monoFont
                }
            }
        }
    }
}
