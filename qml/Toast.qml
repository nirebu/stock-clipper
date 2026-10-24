import QtQuick
import StockClipper 1.0

Rectangle {
    id: toast

    property string kind: "info"
    property bool shown: false

    width: label.implicitWidth + 34
    height: 34
    radius: 8
    color: kind === "error" ? "#3a1d1d" : (kind === "ok" ? "#12301f" : Theme.panel3)
    border.width: 1
    border.color: kind === "error" ? "#5c2a2a" : (kind === "ok" ? "#1d5233" : Theme.line)
    opacity: shown ? 1 : 0
    visible: opacity > 0.01
    z: 400

    Behavior on opacity { NumberAnimation { duration: 170 } }

    function show(message, k) {
        label.text = message
        toast.kind = k || "info"
        toast.shown = true
        timer.restart()
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.text
        font.pixelSize: 12
    }

    Timer {
        id: timer
        interval: 3400
        onTriggered: toast.shown = false
    }
}
