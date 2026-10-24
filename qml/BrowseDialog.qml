import QtQuick
import StockClipper 1.0

Sheet {
    id: dlg

    title: "Choose a folder"
    signal chosen(string path)

    property string startPath: ""
    property string currentPath: ""
    property string parentPath: ""
    property var dirs: []
    property int videoCount: 0

    onOpenChanged: if (open) go(startPath)

    function go(path) {
        const info = App.browse(path)
        currentPath = info.path
        parentPath = info.parent
        dirs = info.dirs
        videoCount = info.videos
    }

    Row {
        width: parent.width
        spacing: 10

        Btn {
            label: "↑ Up"
            small: true
            ghost: true
            active: dlg.parentPath !== "" && dlg.parentPath !== dlg.currentPath
            onClicked: dlg.go(dlg.parentPath)
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 80
            text: dlg.currentPath
            color: Theme.text
            font.pixelSize: 12
            font.family: Theme.monoFont
            elide: Text.ElideMiddle
        }
    }

    Rectangle {
        width: parent.width
        height: 300
        radius: 8
        color: "#0a0c10"
        border.width: 1
        border.color: Theme.line

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            model: dlg.dirs
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property string modelData
                width: list.width
                height: 26
                radius: 5
                color: hover.containsMouse ? Theme.panel3 : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 16
                    text: "▸  " + modelData.split("/").filter(function (s) { return s !== "" }).pop()
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dlg.go(modelData)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No sub-folders"
                color: Theme.muted
                font.pixelSize: 11
            }
        }
    }

    Text {
        text: dlg.videoCount + (dlg.videoCount === 1 ? " video file" : " video files") + " in this folder"
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

        Item {
            width: parent.width - 210
            height: 1
        }
        Btn {
            label: "Cancel"
            ghost: true
            onClicked: dlg.closed()
        }
        Btn {
            label: "Use this folder"
            primary: true
            onClicked: {
                dlg.chosen(dlg.currentPath)
                dlg.closed()
            }
        }
    }
}
