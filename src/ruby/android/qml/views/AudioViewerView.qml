import QtQuick
import QtQuick.Controls
import ".."
import "../components"
import Ruby 1.0

Item {
    id: root

    property string filePath: ""
    property bool loopEnabled: false

    signal backRequested()

    function formatTime(ms) {
        if (!ms || ms < 0) return "0:00"
        var totalSec = Math.floor(ms / 1000)
        var mins = Math.floor(totalSec / 60)
        var secs = totalSec % 60
        return mins + ":" + (secs < 10 ? "0" : "") + secs
    }

    onFilePathChanged: {
        if (filePath.length > 0) {
            audioBridge.load(filePath)
        }
    }

    Component.onDestruction: {
        audioBridge.stop()
    }

    // Background surface
    Rectangle {
        anchors.fill: parent
        color: Theme.surface0
    }

    Column {
        anchors.fill: parent

        // ── Top Bar ────────────────────────────────────────────────────────
        TopBar {
            id: topBar
            width: parent.width
            title: audioBridge.fileName.length > 0 ? audioBridge.fileName : qsTr("Audio Player")
            subtitle: audioBridge.formatName + (audioBridge.fileSizeStr.length > 0 ? (" • " + audioBridge.fileSizeStr) : "")
            subtitleColor: Theme.colorAudio

            onBackClicked: {
                audioBridge.stop()
                root.backRequested()
            }
        }

        Flickable {
            width: parent.width
            height: parent.height - topBar.height
            contentHeight: contentCol.implicitHeight + Theme.dp(Theme.spacingXl)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: contentCol
                x: Theme.dp(Theme.paddingScreen)
                width: parent.width - Theme.dp(Theme.paddingScreen) * 2
                topPadding: Theme.dp(Theme.spacingLg)
                spacing: Theme.dp(Theme.spacingLg)

                // ── Waveform Display Card ───────────────────────────────────
                Rectangle {
                    width: parent.width
                    height: Theme.dp(220)
                    radius: Theme.radiusCard
                    color: Theme.surface1
                    border.color: Theme.border
                    border.width: 1

                    // Subtle background gradient
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusCard
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.alpha(Theme.colorAudio, 0.06) }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }

                    // Waveform Canvas
                    Canvas {
                        id: waveCanvas
                        anchors.fill: parent
                        anchors.margins: Theme.dp(Theme.spacingLg)

                        property var waveform: audioBridge.waveform
                        property real progress: (audioBridge.durationMs > 0)
                                                ? Math.min(1.0, Math.max(0.0, audioBridge.positionMs / audioBridge.durationMs))
                                                : 0.0

                        onWaveformChanged: requestPaint()
                        onProgressChanged: requestPaint()

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)

                            if (!waveform || waveform.length === 0) {
                                // Draw empty baseline
                                ctx.strokeStyle = Theme.borderStrong
                                ctx.lineWidth = 2
                                ctx.beginPath()
                                ctx.moveTo(0, height / 2)
                                ctx.lineTo(width, height / 2)
                                ctx.stroke()
                                return
                            }

                            var n = waveform.length
                            var barW = Math.max(2, (width / n) - 1.5)
                            var midY = height / 2
                            var playheadX = progress * width

                            for (var i = 0; i < n; i++) {
                                var x = (i / n) * width
                                var amp = waveform[i] || 0.1
                                var barH = Math.max(4, amp * (height - Theme.dp(16)))
                                var y = midY - (barH / 2)

                                if (x <= playheadX) {
                                    // Played bars: vibrant audio pink/rose gradient
                                    ctx.fillStyle = Theme.colorAudio
                                } else {
                                    // Unplayed bars: subtle studio dark tone
                                    ctx.fillStyle = Theme.surface3
                                }

                                ctx.beginPath()
                                if (ctx.roundRect) {
                                    ctx.roundRect(x, y, barW, barH, barW / 2)
                                    ctx.fill()
                                } else {
                                    ctx.fillRect(x, y, barW, barH)
                                }
                            }

                            // Draw Playhead Line
                            if (progress > 0 && progress < 1.0) {
                                ctx.strokeStyle = "#FFFFFF"
                                ctx.lineWidth = 2
                                ctx.beginPath()
                                ctx.moveTo(playheadX, 0)
                                ctx.lineTo(playheadX, height)
                                ctx.stroke()

                                // Playhead glowing dot
                                ctx.fillStyle = Theme.colorAudio
                                ctx.beginPath()
                                ctx.arc(playheadX, midY, 5, 0, 2 * Math.PI)
                                ctx.fill()
                            }
                        }

                        // Tap/drag to seek directly on the waveform
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            function updateSeek(mouse) {
                                if (audioBridge.durationMs > 0 && waveCanvas.width > 0) {
                                    var frac = Math.max(0.0, Math.min(1.0, mouse.x / waveCanvas.width))
                                    audioBridge.seek(Math.round(frac * audioBridge.durationMs))
                                }
                            }
                            onPressed: (mouse) => updateSeek(mouse)
                            onPositionChanged: (mouse) => {
                                if (pressed) updateSeek(mouse)
                            }
                        }
                    }

                    // Time Readout Overlay
                    Item {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.dp(Theme.spacingMd)
                        height: Theme.dp(18)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.formatTime(audioBridge.positionMs)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.DemiBold
                            color: Theme.colorAudio
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.formatTime(audioBridge.durationMs)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.DemiBold
                            color: Theme.textMuted
                        }
                    }
                }

                // ── Scrubber Slider ─────────────────────────────────────────
                Slider {
                    id: scrubSlider
                    width: parent.width
                    from: 0
                    to: Math.max(1, audioBridge.durationMs)
                    value: audioBridge.positionMs
                    live: false
                    onMoved: audioBridge.seek(value)
                }

                // ── Primary Control Deck ────────────────────────────────────
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.dp(Theme.spacingLg)

                    // Skip -5s
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "undo"
                        buttonSize: Theme.dp(44)
                        iconSize: Theme.dp(20)
                        variant: "ghost"
                        onClicked: audioBridge.seek(Math.max(0, audioBridge.positionMs - 5000))
                    }

                    // Play / Pause Hero Button
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(64)
                        height: Theme.dp(64)
                        radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.colorAudio }
                            GradientStop { position: 1.0; color: Theme.accentEnd }
                        }
                        border.color: Theme.alpha("#FFFFFF", 0.3)
                        border.width: 1

                        Icon {
                            anchors.centerIn: parent
                            name: audioBridge.isPlaying ? "pause" : "play"
                            size: Theme.dp(28)
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (typeof screenOrientation !== "undefined" && screenOrientation) {
                                    screenOrientation.vibrateTouch(25)
                                }
                                audioBridge.togglePlay()
                            }
                        }
                    }

                    // Skip +5s
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "redo"
                        buttonSize: Theme.dp(44)
                        iconSize: Theme.dp(20)
                        variant: "ghost"
                        onClicked: audioBridge.seek(Math.min(audioBridge.durationMs, audioBridge.positionMs + 5000))
                    }
                }

                // ── Audio Metadata Details ──────────────────────────────────
                SectionCard {
                    width: parent.width
                    label: qsTr("Audio Specifications")
                    accentIcon: "info"

                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        SettingRow {
                            title: qsTr("Format")
                            trailingText: audioBridge.formatName
                        }

                        SettingRow {
                            title: qsTr("Sample Rate")
                            trailingText: qsTr("%1 Hz").arg(audioBridge.sampleRate)
                        }

                        SettingRow {
                            title: qsTr("Channels")
                            trailingText: audioBridge.channels === 1 ? qsTr("Mono (1)") : qsTr("Stereo (2)")
                        }

                        SettingRow {
                            title: qsTr("Bit Depth")
                            trailingText: qsTr("%1-bit").arg(audioBridge.bitsPerSample)
                        }

                        SettingRow {
                            title: qsTr("Duration")
                            trailingText: root.formatTime(audioBridge.durationMs)
                        }

                        SettingRow {
                            title: qsTr("File Size")
                            trailingText: audioBridge.fileSizeStr
                        }
                    }
                }
            }
        }
    }
}
