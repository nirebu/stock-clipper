import QtQuick
import StockClipper 1.0

Window {
    id: win

    width: 1400
    height: 900
    minimumWidth: 1040
    minimumHeight: 680
    visible: true
    color: Theme.bg
    title: App.hasSelection ? (App.curName + " — Stock Clipper") : "Stock Clipper"

    property bool settingsOpen: false
    property bool browseOpen: false
    property bool browseForRoot: true
    property bool queueOpen: true
    property bool loopOn: true

    Item {
        id: root
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                win.settingsOpen = false
                win.browseOpen = false
                confirmAllSheet.open = false
                root.forceActiveFocus()
                event.accepted = true
                return
            }
            if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    App.renderCurrent()
                    event.accepted = true
                }
                return
            }
            if (!App.hasSelection)
                return
            const big = (event.modifiers & Qt.ShiftModifier) !== 0
            switch (event.key) {
            case Qt.Key_Space:
                App.togglePlay()
                event.accepted = true
                break
            case Qt.Key_I:
                App.setInAtPlayhead()
                event.accepted = true
                break
            case Qt.Key_O:
                App.setOutAtPlayhead()
                event.accepted = true
                break
            case Qt.Key_Left:
                App.stepFrame(-1, big)
                event.accepted = true
                break
            case Qt.Key_Right:
                App.stepFrame(1, big)
                event.accepted = true
                break
            case Qt.Key_BracketLeft:
                App.nudgeIn(big ? -1 : -0.1)
                event.accepted = true
                break
            case Qt.Key_BracketRight:
                App.nudgeOut(big ? 1 : 0.1)
                event.accepted = true
                break
            }
        }

        /* ----------------------------- topbar ----------------------------- */

        Item {
            id: topbar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 50

            Rectangle {
                anchors.fill: parent
                color: Theme.panel
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.line
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9

                Rectangle {
                    width: 11
                    height: 11
                    radius: 6
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Stock Clipper"
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                id: sourcePill
                anchors.left: parent.left
                anchors.leftMargin: 178
                anchors.verticalCenter: parent.verticalCenter
                height: 30
                width: Math.max(160, Math.min(430, parent.width - 600))
                radius: 7
                color: Theme.panel2
                border.width: 1
                border.color: Theme.lineSoft

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    spacing: 8

                    Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: App.ffmpegOk ? Theme.ok : Theme.danger
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: parent.width - 78
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.root
                        color: Theme.muted
                        font.pixelSize: 12
                        font.family: Theme.monoFont
                        elide: Text.ElideMiddle
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "change"
                        color: Theme.accent
                        font.pixelSize: 11
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.settingsOpen = true
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Btn {
                    label: "Rescan"
                    ghost: true
                    onClicked: App.rescan()
                }
                Btn {
                    label: "Renders"
                    ghost: true
                    onClicked: App.revealOutputs()
                }
                Btn {
                    label: "Settings"
                    ghost: true
                    onClicked: win.settingsOpen = true
                }
            }
        }

        /* ------------------------------ queue ----------------------------- */

        QueuePanel {
            id: queue
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: implicitHeight
            open: win.queueOpen
        }

        /* ------------------------------ body ------------------------------ */

        Item {
            id: body
            anchors.top: topbar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: queue.top

            /* library */

            Item {
                id: library
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 308

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panel
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.line
                }

                Item {
                    id: libHead
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 44

                    Rectangle {
                        id: searchBox
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 24 - 40
                        height: 28
                        radius: 6
                        color: Theme.panel2
                        border.width: 1
                        border.color: search.activeFocus ? Theme.accentDim : Theme.line

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Filter clips…"
                            color: Theme.muted
                            font.pixelSize: 12
                            visible: search.text === ""
                        }

                        TextInput {
                            id: search
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            height: 20
                            color: Theme.text
                            font.pixelSize: 12
                            selectByMouse: true
                            selectionColor: Theme.accentDim
                            onTextChanged: App.setFilter(text)
                            Keys.onEscapePressed: {
                                text = ""
                                root.forceActiveFocus()
                            }
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: list.count === App.clipCount
                              ? String(App.clipCount)
                              : list.count + "/" + App.clipCount
                        color: Theme.muted
                        font.pixelSize: 11
                        font.family: Theme.monoFont
                    }
                }

                ListView {
                    id: list
                    anchors.top: libHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: libFooter.top
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    clip: true
                    spacing: 2
                    model: App.filteredClips
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: ClipRow {}

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: list.count === 0
                        text: App.clipCount === 0
                              ? "No clips found in\n" + App.root
                              : "No clip matches the filter"
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                }

                Item {
                    id: libFooter
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 50

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.lineSoft
                    }

                    Row {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Btn {
                            width: (parent.width - 8) / 2
                            small: true
                            primary: App.editedCount > 0
                            active: App.editedCount > 0
                            label: App.editedCount > 0
                                   ? "Render edited (" + App.editedCount + ")"
                                   : "Render edited"
                            onClicked: App.renderEdited()
                        }
                        Btn {
                            width: (parent.width - 8) / 2
                            small: true
                            ghost: true
                            active: App.clipCount > 0
                            label: "Render all"
                            onClicked: {
                                if (App.clipCount > 2)
                                    confirmAllSheet.open = true
                                else
                                    App.renderAll()
                            }
                        }
                    }
                }
            }

            /* editor */

            Item {
                id: editor
                anchors.left: library.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom

                Item {
                    id: controls
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 52

                    Row {
                        id: leftGroup
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            minWidth: 38
                            icon: App.playing ? "‖" : "▶"
                            active: App.hasSelection
                            onClicked: App.togglePlay()
                        }
                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "⟨"
                            ghost: true
                            active: App.hasSelection
                            onClicked: App.stepFrame(-1, false)
                        }
                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "⟩"
                            ghost: true
                            active: App.hasSelection
                            onClicked: App.stepFrame(1, false)
                        }
                        TickBox {
                            anchors.verticalCenter: parent.verticalCenter
                            label: "Loop"
                            checked: win.loopOn
                            onToggled: (checked) => {
                                win.loopOn = checked
                                App.setLoop(checked)
                            }
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 20
                            color: Theme.line
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Rotate"
                            color: Theme.muted
                            font.pixelSize: 11
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Repeater {
                                model: [0, 90, 180, 270]
                                delegate: Btn {
                                    required property int modelData
                                    label: modelData + "°"
                                    tiny: true
                                    on: App.rotate === modelData
                                    active: App.hasSelection
                                    onClicked: App.setRotate(modelData)
                                }
                            }
                        }

                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "⇋"
                            ghost: true
                            on: App.flipH
                            active: App.hasSelection
                            onClicked: App.toggleFlipH()
                        }
                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "⇵"
                            ghost: true
                            on: App.flipV
                            active: App.hasSelection
                            onClicked: App.toggleFlipV()
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 20
                            color: Theme.line
                        }

                        TickBox {
                            anchors.verticalCenter: parent.verticalCenter
                            label: "Keep audio"
                            active: App.hasSelection && App.curHasAudio
                            checked: App.keepAudio
                            onToggled: (checked) => App.setAudioChoice(checked)
                        }

                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: controls.width - leftGroup.width > 470
                            text: "space play · I / O set in / out · ←→ nudge"
                            color: Theme.muted
                            font.pixelSize: 11
                        }

                        Btn {
                            anchors.verticalCenter: parent.verticalCenter
                            label: "Render clip"
                            primary: true
                            active: App.hasSelection && App.curError === ""
                            onClicked: App.renderCurrent()
                        }
                    }
                }

                Timeline {
                    id: timeline
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: controls.top
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.bottomMargin: 4
                    height: implicitHeight
                }

                Stage {
                    id: stage
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: timeline.top
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.topMargin: 14
                    anchors.bottomMargin: 6
                }
            }
        }

        /* ---------------------------- overlays ---------------------------- */

        SettingsDialog {
            id: settingsDialog
            anchors.fill: parent
            open: win.settingsOpen
            onClosed: {
                win.settingsOpen = false
                root.forceActiveFocus()
            }
            onBrowseRequested: (forRoot) => {
                win.browseForRoot = forRoot
                browseDialog.startPath = forRoot ? settingsDialog.rootText : settingsDialog.outText
                win.browseOpen = true
            }
        }

        BrowseDialog {
            id: browseDialog
            anchors.fill: parent
            open: win.browseOpen
            onClosed: {
                win.browseOpen = false
                root.forceActiveFocus()
            }
            onChosen: (path) => settingsDialog.setFolder(path, win.browseForRoot)
        }

        Sheet {
            id: confirmAllSheet
            anchors.fill: parent
            title: "Render all clips?"

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Render " + App.clipCount + " clips to " + App.outputDir + "?"
                color: Theme.text
                font.pixelSize: 13
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
                    onClicked: confirmAllSheet.open = false
                }
                Btn {
                    label: "Render all"
                    primary: true
                    onClicked: {
                        confirmAllSheet.open = false
                        App.renderAll()
                    }
                }
            }
        }

        Toast {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 56
        }

        Connections {
            target: App
            function onToast(message, kind) {
                toast.show(message, kind)
            }
        }
    }
}
