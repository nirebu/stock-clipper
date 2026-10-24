import QtQuick
import StockClipper 1.0

Rectangle {
    id: btn

    property string label: ""
    property string icon: ""
    property bool primary: false
    property bool ghost: false
    property bool small: false
    property bool tiny: false
    property bool on: false
    property bool active: true
    property int minWidth: 0

    signal clicked()

    readonly property bool hovered: hover.containsMouse

    implicitHeight: tiny ? 23 : (small ? 27 : 32)
    implicitWidth: Math.max(minWidth, row.implicitWidth + (tiny ? 14 : (small ? 20 : 26)))
    radius: tiny ? 5 : 7
    opacity: active ? 1 : 0.4

    color: {
        if (!active)
            return Theme.panel2
        if (primary)
            return hover.pressed ? Qt.darker(Theme.accent, 1.15) : Theme.accent
        if (on)
            return Theme.panel3
        if (ghost)
            return hover.containsMouse ? Theme.panel3 : "transparent"
        return hover.containsMouse ? Theme.panel3 : Theme.panel2
    }
    border.width: primary ? 0 : 1
    border.color: on ? Theme.accent : (ghost ? "transparent" : Theme.line)

    Behavior on color { ColorAnimation { duration: 90 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Text {
            visible: btn.icon !== ""
            text: btn.icon
            anchors.verticalCenter: parent.verticalCenter
            color: btn.primary ? "#14161a" : Theme.text
            font.pixelSize: btn.tiny ? 11 : (btn.small ? 12 : 13)
        }
        Text {
            visible: btn.label !== ""
            text: btn.label
            anchors.verticalCenter: parent.verticalCenter
            color: btn.primary ? "#14161a" : (btn.ghost && !btn.on ? Theme.muted : Theme.text)
            font.pixelSize: btn.tiny ? 11 : (btn.small ? 12 : 13)
            font.weight: btn.primary ? Font.DemiBold : Font.Medium
        }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: btn.active
        onClicked: btn.clicked()
    }
}
