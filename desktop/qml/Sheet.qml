import QtQuick
import StockClipper 1.0

Item {
    id: sheet

    property string title: ""
    property bool open: false
    default property alias content: bodyColumn.data

    signal closed()

    visible: open
    z: 200

    Rectangle {
        anchors.fill: parent
        color: "#9905070a"

        MouseArea {
            anchors.fill: parent
            onClicked: sheet.closed()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(680, Math.max(440, sheet.width - 140))
        height: column.implicitHeight
        radius: 12
        color: Theme.panel2
        border.width: 1
        border.color: Theme.line

        MouseArea { anchors.fill: parent }

        Column {
            id: column
            width: parent.width

            Item {
                width: column.width
                height: 48

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: sheet.title
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Btn {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "✕"
                    ghost: true
                    small: true
                    onClicked: sheet.closed()
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.lineSoft
                }
            }

            Item {
                width: column.width
                height: bodyColumn.implicitHeight + 32

                Column {
                    id: bodyColumn
                    x: 18
                    y: 16
                    width: parent.width - 36
                    spacing: 13
                }
            }
        }
    }
}
