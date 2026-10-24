import QtQuick
import StockClipper 1.0

Item {
    id: row

    required property string path
    required property string name
    required property string rel
    required property real duration
    required property int displayWidth
    required property int displayHeight
    required property string thumb
    required property var flags
    required property string error
    required property bool loading
    required property bool active
    required property int rotate

    width: ListView.view ? ListView.view.width : 280
    height: 62

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: row.active ? "#221d10" : (hover.containsMouse ? Theme.panel2 : "transparent")
        border.width: 1
        border.color: row.active ? "#6b5220" : "transparent"
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: App.selectClip(row.path)
    }

    Row {
        anchors.fill: parent
        anchors.margins: 7
        spacing: 9

        Item {
            width: 78
            height: 46
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                radius: 5
                color: "#000000"
                border.width: 1
                border.color: "#000"
            }

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: row.thumb
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                visible: row.thumb !== "" && status === Image.Ready
            }

            Text {
                anchors.centerIn: parent
                visible: row.loading || (row.thumb === "" && row.error === "")
                text: row.loading ? "…" : "▦"
                color: Theme.muted
                font.pixelSize: 15
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 2
                height: 14
                width: durText.implicitWidth + 6
                radius: 3
                color: "#b8000000"
                Text {
                    id: durText
                    anchors.centerIn: parent
                    text: App.fmt(row.duration, 1)
                    color: "#dfe4ec"
                    font.pixelSize: 9
                    font.family: Theme.monoFont
                }
            }
        }

        Column {
            width: parent.width - 78 - 26 - 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: row.name
                color: Theme.text
                elide: Text.ElideMiddle
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: row.error !== ""
                      ? row.error
                      : (row.displayWidth > 0 ? row.displayWidth + "×" + row.displayHeight
                                              + "  ·  " + App.fmt(row.duration, 2) : "reading…")
                color: row.error !== "" ? Theme.danger : Theme.muted
                elide: Text.ElideRight
                font.pixelSize: 11
                font.family: Theme.monoFont
            }

            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: row.flags
                    delegate: Rectangle {
                        required property var modelData
                        height: 13
                        width: flagText.implicitWidth + 8
                        radius: 3
                        color: modelData.kind === "err" ? "#3a1d1d"
                             : modelData.kind === "ok" ? "#12301f"
                             : modelData.kind === "warn" ? "#3a2f13"
                             : modelData.kind === "trim" ? "#33280f"
                             : modelData.kind === "rot" ? "#1d2a30"
                             : Theme.panel3
                        Text {
                            id: flagText
                            anchors.centerIn: parent
                            text: modelData.text.toUpperCase()
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.3
                            color: modelData.kind === "err" ? Theme.danger
                                 : modelData.kind === "ok" ? Theme.ok
                                 : modelData.kind === "warn" ? Theme.accent
                                 : modelData.kind === "trim" ? Theme.accent
                                 : modelData.kind === "rot" ? "#8fc7d8"
                                 : Theme.muted
                        }
                    }
                }
            }
        }

        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                radius: 5
                color: rotHover.containsMouse ? Theme.panel3 : "transparent"
                border.width: row.rotate !== 0 ? 1 : 0
                border.color: Theme.accentDim
            }

            Text {
                anchors.centerIn: parent
                text: "↻"
                font.pixelSize: 14
                color: row.rotate !== 0 ? Theme.accent : Theme.muted
            }

            MouseArea {
                id: rotHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: App.rotateRow(row.path)
            }
        }
    }
}
