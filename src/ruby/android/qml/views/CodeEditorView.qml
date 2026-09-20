import QtQuick
import QtQuick.Controls
import ".."
import "../components"
import Ruby 1.0

Item {
    id: root

    property string filePath: ""
    property string fileName: ""
    property bool isDirty: false
    property bool isEditing: false
    property int editorFontSize: 13
    property bool fromVisual: false
    property var savedCameraState: null

    // ── Signal contract ────────────────────────────────────────────────────
    signal backRequested()                              // (hub navigation)
    signal switchToVisualRequested(string filePath)     // "3D View" button
    signal fileDirtyStateChanged(bool dirty)
    signal statusMessage(string message)

    onIsDirtyChanged: fileDirtyStateChanged(root.isDirty)

    onFilePathChanged: {
        if (filePath.length > 0) {
            codeBridge.loadFile(filePath)
            textArea.text = codeBridge.fileContent
            root.isDirty = false
            root.isEditing = false
            var parts = filePath.split("/")
            root.fileName = parts[parts.length - 1]
            highlighterBridge.setFilePath(filePath)
            highlighterBridge.setDocument(textArea.textDocument)
        }
    }

    function tryLeave() {
        if (root.isDirty) unsavedDialog.open()
        else root.backRequested()
    }

    function trySwitchToVisual() {
        if (root.isDirty) {
            unsavedSwitchDialog.open()
        } else {
            root.switchToVisualRequested(root.filePath)
        }
    }

    // ── App bar ────────────────────────────────────────────────────────────
    TopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        title: root.fileName.length > 0 ? root.fileName : qsTr("Editor")
        subtitle: codeBridge.formatName || qsTr("FileRift / Lua")
        subtitleColor: Theme.accentInk

        onBackClicked: root.tryLeave()

        // Dirty indicator
        Rectangle {
            width: Theme.dp(10)
            height: Theme.dp(10)
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.colorWarning
            visible: root.isDirty
        }

        // Diagnostics chip
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: diagRow.implicitWidth + Theme.dp(18)
            height: Theme.dp(30)
            radius: Theme.radiusPill
            color: codeBridge.diagnosticCount > 0
                   ? Theme.alpha(Theme.colorError, 0.30)
                   : Theme.alpha(Theme.colorSuccess, 0.28)
            border.color: codeBridge.diagnosticCount > 0
                          ? Theme.colorErrorBright : Theme.colorSuccessBright
            border.width: 1

            Row {
                id: diagRow
                anchors.centerIn: parent
                spacing: Theme.dp(4)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: codeBridge.diagnosticCount > 0 ? "alert" : "check-circle"
                    size: Theme.dp(13)
                    color: codeBridge.diagnosticCount > 0 ? Theme.colorErrorBright : Theme.colorSuccessBright
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: codeBridge.diagnosticCount > 0
                          ? qsTr("%1 issues").arg(codeBridge.diagnosticCount)
                          : qsTr("Valid")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                    color: codeBridge.diagnosticCount > 0 ? Theme.colorErrorBright : Theme.colorSuccessBright
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: diagnosticsSheet.open(false)
            }
        }

        // Dedicated 3D View Switch Button (for .scene / .scl files)
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.fileName.toLowerCase().endsWith(".scene")
                     || root.fileName.toLowerCase().endsWith(".scl")
            iconName: "view3d"
            variant: "soft"
            iconSize: Theme.dp(19)
            buttonSize: Theme.dp(34)
            onClicked: root.trySwitchToVisual()
        }

        PrimaryButton {
            width: Theme.dp(84)
            anchors.verticalCenter: parent.verticalCenter
            text: root.isDirty ? qsTr("Save") : qsTr("Saved")
            iconName: "save"
            enabled: root.isDirty
            opacity: root.isDirty ? 1.0 : 0.5
            onClicked: {
                if (codeBridge.saveFile(textArea.text)) {
                    root.isDirty = false
                    root.statusMessage(qsTr("File saved"))
                } else {
                    root.statusMessage(qsTr("Save failed"))
                }
            }
        }
    }

    // ── Editor Container (Gutter + Flickable Surface) ──────────────────────
    Item {
        id: editorContainer
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: accessoryBar.visible ? accessoryBar.top : parent.bottom

        readonly property real lineH: Math.max(Theme.dp(root.editorFontSize * 1.45), textArea.cursorRectangle.height > 0 ? textArea.cursorRectangle.height : Theme.dp(18))
        readonly property real gutterWidth: Math.max(Theme.dp(44), Math.floor(Math.log10(Math.max(1, textArea.lineCount)) + 1) * Theme.dp(8) + Theme.dp(20))

        // Synchronized Line Number Gutter (Stays pinned to left during horizontal panning)
        Rectangle {
            id: gutter
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: editorContainer.gutterWidth
            color: Theme.surfaceSunken
            z: 10

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.borderSubtle
            }

            // Virtualized Canvas: only draws numbers for lines visible in flickable viewport
            Canvas {
                id: gutterCanvas
                anchors.fill: parent

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var lineCount = textArea.lineCount
                    if (lineCount <= 0) return

                    ctx.font = Theme.dp(root.editorFontSize) + "px " + Theme.monoFamily
                    ctx.fillStyle = Theme.textDisabled
                    ctx.textAlign = "right"
                    ctx.textBaseline = "top"

                    var lh = editorContainer.lineH
                    var topPad = textArea.topPadding
                    var scrollY = flickable.contentY

                    var firstLine = Math.max(1, Math.floor((scrollY - topPad) / lh) + 1)
                    var lastLine = Math.min(lineCount, Math.ceil((scrollY + flickable.height - topPad) / lh) + 1)

                    var rightMargin = Theme.dp(8)
                    for (var i = firstLine; i <= lastLine; i++) {
                        var y = topPad + (i - 1) * lh - scrollY
                        ctx.fillText("" + i, width - rightMargin, y)
                    }
                }
            }
        }

        // Unbounded Text Surface Flickable
        Flickable {
            id: flickable
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: gutter.right
            anchors.right: parent.right
            contentWidth: Math.max(width, textArea.contentWidth + Theme.dp(64))
            contentHeight: Math.max(height, textArea.contentHeight + Theme.dp(140))
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            onContentYChanged: gutterCanvas.requestPaint()
            onHeightChanged: gutterCanvas.requestPaint()

            TextArea {
                id: textArea
                width: Math.max(flickable.width, contentWidth + Theme.dp(64))
                height: Math.max(flickable.height, contentHeight + Theme.dp(140))
                leftPadding: Theme.dp(Theme.spacingMd)
                rightPadding: Theme.dp(Theme.spacingXl)
                topPadding: Theme.dp(Theme.spacingMd)
                bottomPadding: Theme.dp(64)
                font.family: Theme.monoFamily
                font.pixelSize: Theme.dp(root.editorFontSize)
                color: Theme.textPrimary
                selectionColor: Theme.accentStart
                selectedTextColor: Theme.onAccent
                wrapMode: TextEdit.NoWrap
                readOnly: !root.isEditing
                background: Rectangle { color: Theme.surfaceSunken }
                cursorVisible: root.isEditing

                onLineCountChanged: gutterCanvas.requestPaint()

                onTextChanged: {
                    if (readOnly) return
                    root.isDirty = true
                    codeBridge.analyzeText(textArea.text)
                    gutterCanvas.requestPaint()
                }
            }

            PinchHandler {
                target: null
                onActiveScaleChanged: (delta) => {
                    if (delta > 1.05 && root.editorFontSize < 24) {
                        root.editorFontSize += 1
                        gutterCanvas.requestPaint()
                    } else if (delta < 0.95 && root.editorFontSize > 10) {
                        root.editorFontSize -= 1
                        gutterCanvas.requestPaint()
                    }
                }
            }
        }
    }

    // ── Edit / read-only FAB ───────────────────────────────────────────────
    Rectangle {
        id: editFab
        anchors.right: parent.right
        anchors.bottom: accessoryBar.visible ? accessoryBar.top : parent.bottom
        anchors.margins: Theme.dp(Theme.spacingLg)
        width: Theme.dp(50)
        height: Theme.dp(50)
        radius: width / 2
        visible: !root.isEditing
        z: 15
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.accentEnd }
            GradientStop { position: 1.0; color: Theme.accentStart }
        }
        border.color: Theme.alpha(Theme.accentInk, 0.45)
        border.width: 1

        Icon {
            anchors.centerIn: parent
            name: "pencil"
            size: Theme.dp(21)
            color: Theme.onAccent
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.isEditing = true
                textArea.forceActiveFocus()
            }
        }
    }

    // ── Soft-keyboard accessory bar ────────────────────────────────────────
    KeyboardAccessoryBar {
        id: accessoryBar
        anchors.bottom: parent.bottom
        visible: root.isEditing

        onInsertText: (symbol) => textArea.insert(textArea.cursorPosition, symbol)
        onTabRequested: textArea.insert(textArea.cursorPosition, "    ")
        onUntabRequested: {
            var pos = textArea.cursorPosition
            var lineStart = textArea.text.lastIndexOf("\n", pos - 1) + 1
            var head = textArea.text.substring(lineStart, Math.min(lineStart + 4, textArea.text.length))
            if (head.trim().length === 0 && head.length > 0) {
                textArea.remove(lineStart, lineStart + head.length)
            }
        }
        onUndoRequested: textArea.undo()
        onRedoRequested: textArea.redo()
    }

    // Attached C++ syntax highlighter (pure Lua + FileRift grammar)
    RubyHighlighterBridge {
        id: highlighterBridge
    }

    // ── Diagnostics sheet ──────────────────────────────────────────────────
    BottomSheet {
        id: diagnosticsSheet
        title: qsTr("Diagnostics (%1)").arg(codeBridge.diagnosticCount)
        peekHeight: Theme.dp(300)

        ListView {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            clip: true
            spacing: Theme.dp(Theme.spacingSm)
            model: codeBridge.diagnosticsModel

            delegate: Rectangle {
                width: ListView.view.width
                height: Theme.dp(52)
                radius: Theme.radiusSm
                color: Theme.surface2
                border.color: model.severity === "error" ? Theme.colorErrorBright : Theme.colorWarning
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(Theme.spacingMd)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: model.severity === "error" ? "alert" : "info"
                        size: Theme.dp(Theme.iconSm)
                        color: model.severity === "error" ? Theme.colorErrorBright : Theme.colorWarning
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(20) - Theme.dp(Theme.spacingMd)
                        spacing: Theme.dp(1)

                        Text {
                            width: parent.width
                            text: qsTr("Line %1").arg(model.line)
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.family: Theme.monoFamily
                            color: Theme.textMuted
                        }

                        Text {
                            width: parent.width
                            text: model.message
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        textArea.cursorPosition = textArea.positionAt(0, model.line - 1)
                        diagnosticsSheet.close()
                    }
                }
            }
        }
    }

    // ── Unsaved changes on Exit ────────────────────────────────────────────
    ConfirmDialog {
        id: unsavedDialog
        title: qsTr("Unsaved Changes")
        message: qsTr("“%1” has unsaved edits. Save before leaving?").arg(root.fileName)
        confirmText: qsTr("Save")
        cancelText: qsTr("Discard")
        closePolicy: Popup.CloseOnEscape
        onConfirmed: {
            codeBridge.saveFile(textArea.text)
            root.isDirty = false
            root.backRequested()
        }
        onCancelled: root.backRequested()
    }

    // ── Unsaved changes on Switch to 3D View ────────────────────────────────
    Popup {
        id: unsavedSwitchDialog
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.dp(48) : Theme.dp(340), Theme.dp(380))
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: Theme.dp(Theme.spacingLg)

        Overlay.modal: Rectangle { color: "#B3000000" }

        background: Rectangle {
            radius: Theme.radiusCard
            color: Theme.surface1
            border.color: Theme.border
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            Row {
                spacing: Theme.dp(Theme.spacingSm)
                Rectangle {
                    width: Theme.dp(32)
                    height: Theme.dp(32)
                    radius: Theme.radiusSm
                    color: Theme.alpha(Theme.colorWarning, 0.25)
                    border.color: Theme.colorWarning
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                    Icon {
                        anchors.centerIn: parent
                        name: "alert"
                        size: Theme.dp(16)
                        color: Theme.colorWarning
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Unsaved Code Changes")
                    font.pixelSize: Theme.dp(Theme.fontLg)
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
            }

            Text {
                width: parent.width
                text: qsTr("You have unsaved edits in this file. Would you like to save before opening the 3D visual editor?")
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: unsavedSwitchDialog.close()
                }

                Button {
                    text: qsTr("Discard")
                    flat: true
                    onClicked: {
                        unsavedSwitchDialog.close()
                        root.isDirty = false
                        root.switchToVisualRequested(root.filePath)
                    }
                }

                Button {
                    text: qsTr("Save & Open")
                    highlighted: true
                    onClicked: {
                        codeBridge.saveFile(textArea.text)
                        root.isDirty = false
                        unsavedSwitchDialog.close()
                        root.switchToVisualRequested(root.filePath)
                    }
                }
            }
        }
    }
}
