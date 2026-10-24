import QtQuick
import StockClipper 1.0

Item {
    id: panel

    property bool open: true
    readonly property int bodyH: list.count === 0 ? 30 : Math.min(220, list.count * 40 + 10)

    implicitHeight: 36 + (open ? bodyH : 0)

    Rectangle {
        anchors.fill: parent
        color: Theme.panel
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.open = !panel.open
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Text {
                text: panel.open ? "▾" : "▴"
                color: Theme.muted
                font.pixelSize: 11
            }
            Text {
                text: "Render queue"
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Rectangle {
                visible: App.activeJobs > 0
                height: 16
                width: badge.implicitWidth + 10
                radius: 8
                color: Theme.accentDim
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: App.activeJobs
                    color: Theme.accent
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }
            Text {
                text: App.queueSummary
                color: Theme.muted
                font.pixelSize: 11
                font.family: Theme.monoFont
            }
        }
    }

    ListView {
        id: list
        y: 36
        width: parent.width
        height: panel.open ? panel.bodyH : 0
        visible: panel.open
        clip: true
        model: App.jobs
        boundsBehavior: Flickable.StopAtBounds
        spacing: 0

        delegate: Item {
            required property string jobId
            required property string jobPath
            required property string outPath
            required property string outName
            required property string jobName
            required property string status
            required property real progress
            required property int percent
            required property string metaText
            required property string jobError
            required property bool canPlay
            required property bool canCancel
            required property bool canRetry

            width: list.width
            height: 40

            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: 6
                color: status === "running" ? "#1a1f28" : (status === "error" ? "#241416" : "transparent")
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    text: status === "done" ? "✓"
                        : status === "error" ? "!"
                        : status === "running" ? "▶"
                        : status === "queued" ? "•" : "–"
                    color: status === "done" ? Theme.ok
                         : status === "error" ? Theme.danger
                         : status === "running" ? Theme.accent
                         : Theme.muted
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(120, list.width * 0.3)
                    text: outName
                    color: Theme.text
                    font.pixelSize: 12
                    font.family: Theme.monoFont
                    elide: Text.ElideMiddle
                }

                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(80, list.width * 0.28)
                    height: 5

                    Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: Theme.panel3
                    }
                    Rectangle {
                        width: parent.width * (status === "done" ? 1 : progress)
                        height: parent.height
                        radius: 3
                        color: status === "error" ? Theme.danger : (status === "done" ? Theme.ok : Theme.accent)
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 92
                    text: metaText
                    color: Theme.muted
                    font.pixelSize: 11
                    font.family: Theme.monoFont
                }

                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(46, list.width * 0.16)
                    height: 20

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Text {
                            visible: canPlay
                            text: "play"
                            color: Theme.accent
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.playRendered(outPath)
                            }
                        }
                        Text {
                            visible: canCancel
                            text: "cancel"
                            color: Theme.muted
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.cancelJob(jobId)
                            }
                        }
                        Text {
                            visible: canRetry
                            text: "retry"
                            color: Theme.accent
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.retryJob(jobPath)
                            }
                        }
                    }
                }

                Text {
                    visible: jobError !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - 620)
                    text: jobError
                    color: Theme.danger
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: "Nothing rendered yet"
            color: Theme.muted
            font.pixelSize: 11
        }
    }
}
