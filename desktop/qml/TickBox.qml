import QtQuick
import StockClipper 1.0

Item {
    id: box

    property string label: ""
    property bool checked: false
    property bool active: true
    signal toggled(bool checked)

    implicitHeight: 22
    implicitWidth: row.implicitWidth
    opacity: active ? 1 : 0.4

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Rectangle {
            width: 15
            height: 15
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: box.checked ? Theme.accent : "transparent"
            border.width: 1
            border.color: box.checked ? Theme.accent : (hover.containsMouse ? Theme.muted : Theme.line)

            Text {
                anchors.centerIn: parent
                visible: box.checked
                text: "✓"
                color: "#14161a"
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: box.label
            color: box.active ? Theme.text : Theme.muted
            font.pixelSize: 13
        }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: box.active
        onClicked: box.toggled(!box.checked)
    }
}
