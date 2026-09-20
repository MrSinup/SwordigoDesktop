import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// GroundMeshStudioView.qml — SWDM Ground Mesh Sheet Studio
//
// A landscape-first 2D drafting bench for Swordigo's ground-mesh sheets
// (`.swdm`).  The viewport is a coordinatised sheet on the level's XY plane;
// you draft the top-surface polygon and the studio extrudes it into a
// game-ready mesh on export.
//
// ── Why this file was rewritten ─────────────────────────────────────────────
//
// 1. NODES COULD NOT BE MOVED.  The old editor kept `property var points` and
//    rebuilt the whole array on every drag event:
//
//        root.points = copy            // <-- inside MouseArea.onPositionChanged
//
//    A `Repeater` cannot update a plain JS array in place, so assigning a new
//    array forces a full model reset: every delegate is destroyed and rebuilt.
//    The delegate under the finger was therefore destroyed mid-gesture, which
//    released its mouse grab, so the node stopped moving after the first event.
//    The fix is structural, not cosmetic: the nodes now live in a `ListModel`
//    and drags mutate one role with `setProperty()`, which emits a targeted
//    dataChanged and leaves every delegate — and its grab — alive.
//
// 2. THE LAYOUT WAS PORTRAIT.  A fixed 220dp dock sat on the right edge of a
//    landscape window, so the drafting sheet only ever got ~60% of the width,
//    and the toolbar was pinned above it rather than floating.  The dock is now
//    an overlay that is closed by default, the toolbar floats, and the sheet
//    owns the entire surface.
//
// 3. IT REFERENCED TWO THEME TOKENS THAT DO NOT EXIST.  `Theme.backgroundDark`
//    and `Theme.paddingSm` are not in Theme.qml, so the backdrop was an
//    undefined colour and every one of those margins evaluated to NaN.  Now
//    `Theme.surface0` / `Theme.spacingSm`.
//
// ── Gestures ────────────────────────────────────────────────────────────────
//
//   drag a node ............ move it (snapped, sub-pixel accumulate)
//   two-finger pinch ....... zoom about the pinch centroid
//   drag empty sheet ....... pan
//   double-tap a node ...... delete it
//   long-press a node ...... node menu (insert neighbour / duplicate / delete)
//   tap an edge ............ insert a node exactly on that edge
//   tap empty space ........ append a node at the end of the ring
//
// Every mutation is undoable.  Haptics fire on grab and on commit.
// ============================================================================
Item {
    id: root

    // ── Public API (Main.qml pushes/pops this view; nothing else is contract) ─
    property string currentFilePath: ""

    // ── Terrain parameters ─────────────────────────────────────────────────
    property real minDepth: -45.0
    property real maxDepth: 45.0
    property real surfaceWidth: 80.0
    property string topTexture: "fire_grass"
    property string frontTexture: "graveyard_ground"

    // ── Canvas view transform ──────────────────────────────────────────────
    property real zoomScale: 0.85
    property real panOffsetX: 0.0
    property real panOffsetY: 0.0

    readonly property real kMinZoom: 0.10
    readonly property real kMaxZoom: 5.0
    readonly property real kSheet: 40.0        // design-unit grid pitch

    // ── Editor state ───────────────────────────────────────────────────────
    /// Index into `nodes` of the highlighted vertex, or -1.  Declared here
    /// explicitly: every function below writes it unqualified, and an
    /// undeclared identifier does not throw at the call site — it compiles to a
    /// write to a global that fails silently and leaves the selection stale.
    property int selectedPoint: -1
    /// "select" | "insert" | "delete" | "pan"
    property string tool: "select"
    /// Grid snap step in world units; 0 disables snapping.
    property real snapStep: 5.0
    property bool dockOpen: false
    property bool showGrid: true
    /// World-space pointer position, for the readout HUD.
    property real cursorX: 0.0
    property real cursorY: 0.0
    property bool cursorValid: false

    readonly property int pointCount: nodes.count
    readonly property int kMaxUndo: 64
    property var undoStack: []
    property var redoStack: []
    readonly property bool canUndo: undoStack.length > 0
    readonly property bool canRedo: redoStack.length > 0

    /// Landscape (or at least wide enough) to host the drafting bench.
    readonly property bool studioFits: root.width >= Theme.dp(560) && root.width > root.height

    signal backRequested()

    Component.onCompleted: {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockLandscape()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockLandscape()
        canvas.requestPaint()
    }

    Component.onDestruction: {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockPortrait()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockPortrait()
    }

    StackView.onActivated: {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockLandscape()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockLandscape()
    }

    // ========================================================================
    //  Node store
    // ========================================================================
    //
    // ListModel, not a JS array.  `setProperty` updates one role in place, so
    // the Repeater keeps the delegate the finger is holding down on.
    ListModel {
        id: nodes
        Component.onCompleted: root.resetTo([
            { x: -180.0, y: -60.0 },
            { x:  180.0, y: -60.0 },
            { x:  150.0, y:  70.0 },
            { x: -150.0, y:  70.0 }
        ])
    }

    // ========================================================================
    //  Coordinate transforms
    // ========================================================================
    function worldToScreenX(wx) { return canvasArea.width  * 0.5 + (wx + panOffsetX) * zoomScale }
    function worldToScreenY(wy) { return canvasArea.height * 0.5 - (wy + panOffsetY) * zoomScale }
    function screenToWorldX(sx) { return ((sx - canvasArea.width  * 0.5) / zoomScale) - panOffsetX }
    function screenToWorldY(sy) { return -(((sy - canvasArea.height * 0.5) / zoomScale) + panOffsetY) }

    function snapValue(v) {
        if (snapStep <= 0.0) return Math.round(v * 100) / 100
        return Math.round(v / snapStep) * snapStep
    }

    // ========================================================================
    //  Geometry helpers
    // ========================================================================
    function distToSegment(px, py, ax, ay, bx, by) {
        var vx = bx - ax, vy = by - ay
        var len2 = vx * vx + vy * vy
        if (len2 <= 1e-9) return Math.hypot(px - ax, py - ay)
        var t = Math.max(0.0, Math.min(1.0, ((px - ax) * vx + (py - ay) * vy) / len2))
        return Math.hypot(px - (ax + t * vx), py - (ay + t * vy))
    }

    /// Closest ring edge to a world point.  Returns { index, dist, x, y } where
    /// index is the segment's start vertex and (x, y) is the projection onto it.
    function closestEdge(wx, wy) {
        var n = nodes.count
        if (n < 2) return { index: -1, dist: 1e18, x: wx, y: wy }
        var best = -1, bestD = 1e18, bestX = wx, bestY = wy
        for (var i = 0; i < n; ++i) {
            var a = nodes.get(i)
            var b = nodes.get((i + 1) % n)
            var d = distToSegment(wx, wy, a.px, a.py, b.px, b.py)
            if (d < bestD) {
                bestD = d
                best = i
                bestX = projectOnSegment(wx, wy, a.px, a.py, b.px, b.py).x
                bestY = projectOnSegment(wx, wy, a.px, a.py, b.px, b.py).y
            }
        }
        return { index: best, dist: bestD, x: bestX, y: bestY }
    }

    function projectOnSegment(wx, wy, ax, ay, bx, by) {
        var vx = bx - ax, vy = by - ay
        var len2 = vx * vx + vy * vy
        if (len2 <= 1e-9) return { x: ax, y: ay }
        var t = Math.max(0.08, Math.min(0.92, ((wx - ax) * vx + (wy - ay) * vy) / len2))
        return { x: ax + t * vx, y: ay + t * vy }
    }

    function toList() {
        var out = []
        for (var i = 0; i < nodes.count; ++i) {
            var p = nodes.get(i)
            out.push({ x: p.px, y: p.py })
        }
        return out
    }

    // ========================================================================
    //  Mutation + undo
    // ========================================================================
    function pushUndo(snapshot) {
        var s = undoStack.slice()
        s.push(snapshot === undefined ? toList() : snapshot)
        while (s.length > kMaxUndo) s.shift()
        undoStack = s
        redoStack = []
    }

    function restore(list) {
        nodes.clear()
        for (var i = 0; i < list.length; ++i)
            nodes.append({ px: list[i].x, py: list[i].y })
        if (selectedPoint >= nodes.count) selectedPoint = nodes.count - 1
        canvas.requestPaint()
    }

    function undo() {
        if (undoStack.length === 0) return
        var s = undoStack.slice()
        var prev = s.pop()
        var r = redoStack.slice()
        r.push(toList())
        undoStack = s
        redoStack = r
        restore(prev)
        buzz(12)
    }

    function redo() {
        if (redoStack.length === 0) return
        var r = redoStack.slice()
        var next = r.pop()
        var s = undoStack.slice()
        s.push(toList())
        undoStack = s
        redoStack = r
        restore(next)
        buzz(12)
    }

    function resetTo(list) {
        nodes.clear()
        for (var i = 0; i < list.length; ++i)
            nodes.append({ px: list[i].x, py: list[i].y })
        undoStack = []
        redoStack = []
        selectedPoint = list.length > 0 ? 0 : -1
        canvas.requestPaint()
    }

    /// Insert a vertex at `index` in the ring.  `before` is the state to store
    /// for undo — the live state if omitted.
    function insertPointAt(index, wx, wy, before) {
        var i = Math.max(0, Math.min(nodes.count, index))
        pushUndo(before)
        nodes.insert(i, { px: snapValue(wx), py: snapValue(wy) })
        selectedPoint = i
        canvas.requestPaint()
        buzz(15)
    }

    function appendPoint(wx, wy) {
        pushUndo()
        nodes.append({ px: snapValue(wx), py: snapValue(wy) })
        selectedPoint = nodes.count - 1
        canvas.requestPaint()
        buzz(15)
    }

    /// A ring needs three vertices to enclose anything, and a two-vertex ring
    /// exports as an empty sheet, so the floor is enforced here rather than
    /// only in the toolbar button's `enabled` state.
    function removePointAt(index) {
        if (nodes.count <= 3 || index < 0 || index >= nodes.count) return false
        pushUndo()
        nodes.remove(index)
        if (selectedPoint >= nodes.count) selectedPoint = nodes.count - 1
        canvas.requestPaint()
        buzz(20)
        return true
    }

    function movePoint(index, wx, wy) {
        if (index < 0 || index >= nodes.count) return
        nodes.setProperty(index, "px", wx)
        nodes.setProperty(index, "py", wy)
        canvas.requestPaint()
    }

    function selectNearest(wx, wy, maxWorldDist) {
        var best = -1, bestD = maxWorldDist
        for (var i = 0; i < nodes.count; ++i) {
            var p = nodes.get(i)
            var d = Math.hypot(p.px - wx, p.py - wy)
            if (d < bestD) { bestD = d; best = i }
        }
        if (best >= 0) selectedPoint = best
        return best
    }

    function nudgeSelected(dx, dy) {
        if (selectedPoint < 0 || selectedPoint >= nodes.count) return
        var p = nodes.get(selectedPoint)
        pushUndo()
        movePoint(selectedPoint, snapValue(p.px + dx), snapValue(p.py + dy))
    }

    function fitToView() {
        if (nodes.count === 0) return
        var minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18
        for (var i = 0; i < nodes.count; ++i) {
            var p = nodes.get(i)
            minX = Math.min(minX, p.px); maxX = Math.max(maxX, p.px)
            minY = Math.min(minY, p.py); maxY = Math.max(maxY, p.py)
        }
        var w = Math.max(1.0, maxX - minX)
        var h = Math.max(1.0, maxY - minY)
        var pad = 1.35
        zoomScale = Math.max(kMinZoom, Math.min(kMaxZoom,
            Math.min(canvasArea.width / (w * pad), canvasArea.height / (h * pad))))
        panOffsetX = 0.0
        panOffsetY = 0.0
        canvas.requestPaint()
    }

    function zoomBy(factor) {
        setZoomAbout(zoomScale * factor, canvasArea.width * 0.5, canvasArea.height * 0.5)
    }

    function setZoomAbout(z, cx, cy) {
        var clamped = Math.max(kMinZoom, Math.min(kMaxZoom, z))
        var awx = screenToWorldX(cx)
        var awy = screenToWorldY(cy)
        zoomScale = clamped
        // Re-derive the pan so the world point under (cx, cy) does not move.
        panOffsetX = (cx - canvasArea.width  * 0.5) / clamped - awx
        panOffsetY = (canvasArea.height * 0.5 - cy) / clamped - awy
        canvas.requestPaint()
    }

    function buzz(ms) {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(ms)
    }

    function loadPreset(name) {
        if (name === "Platform") {
            resetTo([
                { x: -220.0, y: -50.0 },
                { x:  220.0, y: -50.0 },
                { x:  200.0, y:  60.0 },
                { x: -200.0, y:  60.0 }
            ])
        } else if (name === "Slope") {
            resetTo([
                { x: -200.0, y: -80.0 },
                { x:  200.0, y: -80.0 },
                { x:  200.0, y: 120.0 },
                { x: -200.0, y: -10.0 }
            ])
        } else if (name === "Bridge") {
            resetTo([
                { x: -260.0, y: -20.0 },
                { x: -120.0, y: -20.0 },
                { x: -120.0, y: -60.0 },
                { x:  120.0, y: -60.0 },
                { x:  120.0, y: -20.0 },
                { x:  260.0, y: -20.0 },
                { x:  260.0, y:  20.0 },
                { x: -260.0, y:  20.0 }
            ])
        } else if (name === "Floating Island") {
            resetTo([
                { x: -160.0, y:   40.0 },
                { x:  -90.0, y:  -90.0 },
                { x:    0.0, y: -130.0 },
                { x:  100.0, y:  -80.0 },
                { x:  170.0, y:   35.0 },
                { x:    0.0, y:   55.0 }
            ])
        } else if (name === "Stairs") {
            resetTo([
                { x: -200.0, y: 0.0 },
                { x: -100.0, y: 0.0 },
                { x: -100.0, y: 40.0 },
                { x:    0.0, y: 40.0 },
                { x:    0.0, y: 80.0 },
                { x:  100.0, y: 80.0 },
                { x:  100.0, y: 120.0 },
                { x:  200.0, y: 120.0 },
                { x:  200.0, y: -40.0 },
                { x: -200.0, y: -40.0 }
            ])
        }
        panOffsetX = 0.0
        panOffsetY = 0.0
        fitToView()
    }

    // ========================================================================
    //  Backdrop
    // ========================================================================
    Rectangle {
        anchors.fill: parent
        color: Theme.surface0
    }

    // ========================================================================
    //  Drafting sheet — full bleed, behind the floating chrome
    // ========================================================================
    Item {
        id: canvasArea
        anchors.fill: parent
        clip: true

        // Component.onCompleted runs before the first layout pass, so the
        // canvas is still 0x0 there and a fit computed at that moment collapses
        // the zoom to the minimum.  Fit on the first real size instead.
        property bool didInitialFit: false
        function tryInitialFit() {
            if (didInitialFit || width <= 0 || height <= 0) return
            didInitialFit = true
            root.fitToView()
        }
        onWidthChanged: tryInitialFit()
        onHeightChanged: tryInitialFit()

        Canvas {
            id: canvas
            anchors.fill: parent
            renderStrategy: Canvas.Cooperative

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                var cx = width  * 0.5 + root.panOffsetX * root.zoomScale
                var cy = height * 0.5 - root.panOffsetY * root.zoomScale

                // 1. Sheet grid.  Minor lines at the design pitch, major every
                //    fifth, so the canvas reads as engineering paper rather than
                //    a uniform mesh.
                if (root.showGrid) {
                    var minor = root.kSheet * root.zoomScale
                    if (minor >= 6.0) {
                        var major = minor * 5.0
                        var x0 = cx % minor, y0 = cy % minor
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        for (var x = x0; x < width; x += minor) {
                            var isMajorX = Math.abs((Math.round((x - cx) / minor)) % 5) === 0
                            ctx.strokeStyle = isMajorX ? "#1E2532" : "#171C26"
                            ctx.beginPath()
                            ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                        }
                        for (var y = y0; y < height; y += minor) {
                            var isMajorY = Math.abs((Math.round((y - cy) / minor)) % 5) === 0
                            ctx.strokeStyle = isMajorY ? "#1E2532" : "#171C26"
                            ctx.beginPath()
                            ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                        }
                    }
                }

                // 2. Axes — X red, Y green, apex marked at the origin.
                ctx.lineWidth = 1.5
                ctx.strokeStyle = "rgba(239, 68, 68, 0.42)"
                ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(width, cy); ctx.stroke()
                ctx.strokeStyle = "rgba(34, 197, 94, 0.42)"
                ctx.beginPath(); ctx.moveTo(cx, 0); ctx.lineTo(cx, height); ctx.stroke()

                // 3. Polygon body.
                var n = root.pointCount
                if (n >= 2) {
                    ctx.beginPath()
                    var p0 = nodes.get(0)
                    ctx.moveTo(root.worldToScreenX(p0.px), root.worldToScreenY(p0.py))
                    for (var i = 1; i < n; ++i) {
                        var p = nodes.get(i)
                        ctx.lineTo(root.worldToScreenX(p.px), root.worldToScreenY(p.py))
                    }
                    if (n >= 3) {
                        ctx.closePath()
                        var g = ctx.createLinearGradient(0, 0, 0, height)
                        g.addColorStop(0.0, "rgba(16, 185, 129, 0.22)")
                        g.addColorStop(1.0, "rgba(16, 185, 129, 0.06)")
                        ctx.fillStyle = g
                        ctx.fill()
                    }

                    // Edge midline: a hairline down the middle of the stroke so
                    // the surface reads as a solid slab, not a wireframe.
                    ctx.lineWidth = 2.5
                    ctx.strokeStyle = "#10b981"
                    ctx.stroke()
                    ctx.lineWidth = 1.0
                    ctx.strokeStyle = "rgba(209, 250, 229, 0.55)"
                    ctx.stroke()
                }
            }
        }

        // ── Layer 1: pan / tap-to-edit over the bare sheet ──────────────────
        MouseArea {
            id: sheetMouse
            anchors.fill: parent
            z: 1
            hoverEnabled: true
            preventStealing: true

            property real panStartX: 0
            property real panStartY: 0
            property bool didPan: false
            property bool pressOnNode: false

            function refreshCursor(mx, my) {
                root.cursorX = root.screenToWorldX(mx)
                root.cursorY = root.screenToWorldY(my)
                root.cursorValid = true
            }

            onPressed: (mouse) => {
                panStartX = mouse.x
                panStartY = mouse.y
                didPan = false
                pressOnNode = false
                refreshCursor(mouse.x, mouse.y)
            }

            onPositionChanged: (mouse) => {
                refreshCursor(mouse.x, mouse.y)
                if (!pressed) return
                var dx = mouse.x - panStartX
                var dy = mouse.y - panStartY
                // In select mode a one-finger swipe anywhere on the sheet is a
                // pan; in insert/delete mode the same swipe only pans once it
                // clearly leaves tap range, so a sloppy tap still registers.
                var threshold = (root.tool === "select" || root.tool === "pan") ? 6.0 : 14.0
                if (Math.hypot(dx, dy) > threshold) {
                    didPan = true
                    root.panOffsetX += dx / root.zoomScale
                    root.panOffsetY -= dy / root.zoomScale
                    panStartX = mouse.x
                    panStartY = mouse.y
                    canvas.requestPaint()
                }
            }

            onClicked: (mouse) => {
                if (didPan) return
                refreshCursor(mouse.x, mouse.y)
                var wx = root.screenToWorldX(mouse.x)
                var wy = root.screenToWorldY(mouse.y)

                if (root.tool === "pan") {
                    root.selectedPoint = -1
                    return
                }

                if (root.tool === "delete") {
                    var vi = root.selectNearest(wx, wy, 18.0 / root.zoomScale)
                    if (vi >= 0) root.removePointAt(vi)
                    return
                }

                // Tap-to-split: anywhere within ~22 screen px of the ring inserts
                // a vertex exactly on that edge.  This is the primary way to add
                // detail without disturbing the rest of the silhouette.
                var edge = root.closestEdge(wx, wy)
                if (edge.index >= 0 && edge.dist * root.zoomScale <= 22.0) {
                    root.insertPointAt(edge.index + 1, edge.x, edge.y)
                    return
                }

                if (root.tool === "insert") {
                    root.insertPointAt(nodes.count, wx, wy)
                    return
                }

                // Select mode, empty sheet: append at the end of the ring.
                root.appendPoint(wx, wy)
            }
        }

        // ── Layer 2: pinch zoom about the gesture centroid ──────────────────
        PinchHandler {
            id: pinch
            target: null
            minimumPointCount: 2
            maximumPointCount: 2
            property real baseZoom: 1.0
            property real anchorWx: 0.0
            property real anchorWy: 0.0

            onActiveChanged: {
                if (active) {
                    baseZoom = root.zoomScale
                    anchorWx = root.screenToWorldX(centroid.position.x)
                    anchorWy = root.screenToWorldY(centroid.position.y)
                }
            }

            onScaleChanged: {
                if (!active) return
                var z = Math.max(root.kMinZoom, Math.min(root.kMaxZoom, baseZoom * scale))
                root.zoomScale = z
                // Keep the world point that was under the centroid pinned there,
                // which makes a moving two-finger centroid pan as a side effect.
                root.panOffsetX = (centroid.position.x - canvasArea.width  * 0.5) / z - anchorWx
                root.panOffsetY = (canvasArea.height * 0.5 - centroid.position.y) / z - anchorWy
                canvas.requestPaint()
            }
        }

        // ── Layer 3: vertex handles ─────────────────────────────────────────
        Repeater {
            model: nodes

            Item {
                id: handle

                // Named so the QML regression test can address handles and prove
                // they survive a move (the old array-reset model destroyed them).
                objectName: "nodeHandle"

                required property int index
                required property real px
                required property real py

                readonly property bool isSelected: index === root.selectedPoint
                readonly property real grabRadius: Theme.dp(26)

                // Sub-pixel accumulation: a snapped write is only applied to the
                // model, while the raw float keeps integrating the finger, so a
                // slow drag still advances on the grid instead of stalling.
                property real dragRawX: 0
                property real dragRawY: 0
                property real dragStartX: 0
                property real dragStartY: 0
                property bool dragMoved: false
                property var dragSnapshot: null

                z: 3
                width: grabRadius * 2
                height: grabRadius * 2
                x: root.worldToScreenX(px) - width * 0.5
                y: root.worldToScreenY(py) - height * 0.5

                Rectangle {
                    id: dot
                    anchors.centerIn: parent
                    width: handle.isSelected ? Theme.dp(22) : Theme.dp(16)
                    height: width
                    radius: width * 0.5
                    color: handle.isSelected ? Theme.accentInk : Theme.surface1
                    border.color: handle.isSelected ? "#FFFFFF" : Theme.accentInk
                    border.width: handle.isSelected ? 2.5 : 1.5

                    Behavior on width { NumberAnimation { duration: Theme.durFast } }

                    Rectangle {
                        anchors.centerIn: parent
                        width: Theme.dp(5)
                        height: width
                        radius: width * 0.5
                        color: handle.isSelected ? "#FFFFFF" : Theme.accentInk
                    }
                }

                // Crosshair ticks: cheap, and they make a small handle precise to
                // aim at without inflating the touch target.
                Rectangle {
                    visible: handle.isSelected
                    anchors.centerIn: parent
                    width: Theme.dp(30); height: 1
                    color: Theme.alpha(Theme.accentInk, 0.55)
                }
                Rectangle {
                    visible: handle.isSelected
                    anchors.centerIn: parent
                    width: 1; height: Theme.dp(30)
                    color: Theme.alpha(Theme.accentInk, 0.55)
                }

                // Label only the selection — labelling every vertex in a
                // landscape strip buried the geometry under its own metadata.
                Rectangle {
                    visible: handle.isSelected
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom
                    anchors.topMargin: Theme.dp(2)
                    height: labelText.implicitHeight + Theme.dp(6)
                    width: labelText.implicitWidth + Theme.dp(12)
                    radius: Theme.radiusXs
                    color: Theme.alpha(Theme.surface0, 0.86)
                    border.color: Theme.alpha(Theme.accentInk, 0.35)
                    border.width: 1

                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: "#" + (handle.index + 1) + "  " +
                              Math.round(handle.px) + ", " + Math.round(handle.py)
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.family: Theme.fontFamilyMono
                        color: Theme.accentInk
                    }
                }

                MouseArea {
                    id: nodeMouse
                    anchors.fill: parent
                    preventStealing: true

                    onPressed: (mouse) => {
                        handle.dragStartX = mouse.x
                        handle.dragStartY = mouse.y
                        handle.dragRawX = handle.px
                        handle.dragRawY = handle.py
                        handle.dragSnapshot = root.toList()
                        handle.dragMoved = false
                        root.selectedPoint = handle.index
                        root.cursorX = handle.px
                        root.cursorY = handle.py
                        root.cursorValid = true
                        root.buzz(14)
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        handle.dragRawX += (mouse.x - handle.dragStartX) / root.zoomScale
                        handle.dragRawY -= (mouse.y - handle.dragStartY) / root.zoomScale
                        handle.dragStartX = mouse.x
                        handle.dragStartY = mouse.y
                        handle.dragMoved = true
                        root.cursorX = handle.dragRawX
                        root.cursorY = handle.dragRawY
                        root.movePoint(handle.index,
                                       root.snapValue(handle.dragRawX),
                                       root.snapValue(handle.dragRawY))
                    }

                    onReleased: {
                        // One undo entry per gesture, recorded from the state the
                        // node had before the finger landed.
                        if (handle.dragMoved && handle.dragSnapshot)
                            root.pushUndo(handle.dragSnapshot)
                        handle.dragSnapshot = null
                    }

                    onDoubleClicked: {
                        root.removePointAt(handle.index)
                    }

                    onPressAndHold: {
                        nodeMenu.targetIndex = handle.index
                        nodeMenu.popup()
                    }

                    onCanceled: {
                        handle.dragSnapshot = null
                    }
                }
            }
        }

        // ── Layer 4: readout + zoom HUD ─────────────────────────────────────
        Rectangle {
            id: readout
            z: 5
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.dp(10)
            height: Theme.dp(24)
            width: readoutText.implicitWidth + Theme.dp(18)
            radius: Theme.radiusXs
            color: Theme.alpha(Theme.surface0, 0.82)
            border.color: Theme.borderSubtle
            border.width: 1

            Text {
                id: readoutText
                anchors.centerIn: parent
                text: cursorValid
                      ? ("X " + Math.round(cursorX) + "   Y " + Math.round(cursorY))
                      : qsTr("tap a node to inspect")
                font.pixelSize: Theme.dp(Theme.fontXs)
                font.family: Theme.fontFamilyMono
                color: Theme.textSecondary
            }
        }

        // Zoom column — dead vertical space in landscape is better spent on
        // controls than on margin.
        Column {
            id: zoomPill
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.dp(10)
            spacing: Theme.dp(2)

            IconButton {
                variant: "soft"
                square: true
                iconName: "plus"
                buttonSize: Theme.dp(30)
                iconSize: Theme.dp(14)
                tooltip: qsTr("Zoom in")
                onClicked: root.zoomBy(1.25)
            }

            Rectangle {
                width: Theme.dp(30)
                height: Theme.dp(22)
                radius: Theme.radiusXs
                color: Theme.alpha(Theme.surface0, 0.82)
                border.color: Theme.borderSubtle
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "" + Math.round(root.zoomScale * 100) + "%"
                    font.pixelSize: Theme.dp(9)
                    font.family: Theme.fontFamilyMono
                    color: Theme.textSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.fitToView()
                    onClicked: root.fitToView()
                }
            }

            IconButton {
                variant: "soft"
                square: true
                iconName: "minus"
                buttonSize: Theme.dp(30)
                iconSize: Theme.dp(14)
                tooltip: qsTr("Zoom out")
                onClicked: root.zoomBy(0.8)
            }

            IconButton {
                variant: "soft"
                square: true
                iconName: "target"
                buttonSize: Theme.dp(30)
                iconSize: Theme.dp(14)
                tooltip: qsTr("Fit sheet to view")
                onClicked: root.fitToView()
            }
        }
    }

    // ========================================================================
    //  Floating toolbar — overlays the sheet, full landscape width
    // ========================================================================
    Rectangle {
        id: toolbar
        z: 20
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.dp(8)
        height: Theme.dp(42)
        radius: Theme.radiusMd
        color: Theme.alpha(Theme.surface1, 0.94)
        border.color: Theme.borderSubtle
        border.width: 1

        Row {
            id: toolRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            IconButton {
                iconName: "arrow-left"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(16)
                tooltip: qsTr("Back")
                onClicked: root.backRequested()
            }

            Rectangle { width: 1; height: Theme.dp(20); color: Theme.borderSubtle; anchors.verticalCenter: parent.verticalCenter }
        }

        // Tool selector.  Icon-only so four tools fit beside the title on a
        // 900dp landscape strip; each carries a tooltip and a selected plate.
        Row {
            id: toolPicker
            anchors.left: toolRow.right
            anchors.leftMargin: Theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            Repeater {
                model: [
                    { id: "select", icon: "move",  hint: qsTr("Select & move (drag a node)") },
                    { id: "insert", icon: "plus",  hint: qsTr("Insert (tap the sheet or an edge)") },
                    { id: "delete", icon: "trash", hint: qsTr("Delete (tap a node)") },
                    { id: "pan",    icon: "drag",  hint: qsTr("Pan the sheet") }
                ]

                IconButton {
                    variant: root.tool === modelData.id ? "primary" : "plain"
                    square: true
                    iconName: modelData.icon
                    buttonSize: Theme.dp(32)
                    iconSize: Theme.dp(15)
                    tooltip: modelData.hint
                    onClicked: {
                        root.tool = modelData.id
                        root.buzz(10)
                    }
                }
            }
        }

        Column {
            anchors.left: toolPicker.right
            anchors.leftMargin: Theme.dp(10)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                text: qsTr("Ground Mesh Sheet Studio")
                font.pixelSize: Theme.dp(Theme.fontMd)
                font.weight: Font.Bold
                color: Theme.textPrimary
                elide: Text.ElideRight
                width: Math.min(implicitWidth, toolbar.width * 0.30)
            }

            Text {
                text: qsTr("%1 vertices · %2 tool").arg(root.pointCount).arg(root.tool)
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: Theme.textMuted
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            IconButton {
                iconName: "undo"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(15)
                tooltip: qsTr("Undo")
                enabled: root.canUndo
                onClicked: root.undo()
            }

            IconButton {
                iconName: "redo"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(15)
                tooltip: qsTr("Redo")
                enabled: root.canRedo
                onClicked: root.redo()
            }

            IconButton {
                iconName: "trash"
                variant: "danger"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(15)
                tooltip: qsTr("Delete selected vertex")
                enabled: root.selectedPoint >= 0 && root.pointCount > 3
                onClicked: root.removePointAt(root.selectedPoint)
            }

            Rectangle { width: 1; height: Theme.dp(20); color: Theme.borderSubtle; anchors.verticalCenter: parent.verticalCenter }

            IconButton {
                iconName: "grid"
                variant: root.snapStep > 0 ? "soft" : "plain"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(15)
                tooltip: root.snapStep > 0
                         ? qsTr("Snapping to %1 units — tap to change").arg(root.snapStep)
                         : qsTr("Snapping off — tap to change")
                onClicked: {
                    // 0 (off) -> 5 -> 10 -> 25 -> back to off
                    if (root.snapStep === 0) root.snapStep = 5
                    else if (root.snapStep < 10) root.snapStep = 10
                    else if (root.snapStep < 25) root.snapStep = 25
                    else root.snapStep = 0
                }
            }

            IconButton {
                iconName: "sliders-vertical"
                variant: root.dockOpen ? "soft" : "plain"
                square: true
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(15)
                tooltip: qsTr("Terrain parameters")
                onClicked: root.dockOpen = !root.dockOpen
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(30)
                width: exportRow.implicitWidth + Theme.dp(18)
                radius: Theme.radiusSm
                color: exportMouse.pressed ? Theme.accentEnd : Theme.accentStart

                Row {
                    id: exportRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)
                    Icon { name: "drive"; size: Theme.dp(13); color: "#FFFFFF" }
                    Text {
                        text: qsTr("Export")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Bold
                        color: "#FFFFFF"
                    }
                }

                MouseArea {
                    id: exportMouse
                    anchors.fill: parent
                    onClicked: root.doExport()
                }
            }
        }
    }

    // ========================================================================
    //  Parameter dock — overlay, closed by default so the sheet keeps the width
    // ========================================================================
    Rectangle {
        id: dock
        z: 19
        anchors.top: toolbar.bottom
        anchors.topMargin: Theme.dp(4)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.dp(8)
        anchors.bottomMargin: Theme.dp(8)
        width: Theme.dp(236)
        radius: Theme.radiusMd
        color: Theme.alpha(Theme.surface1, 0.96)
        border.color: Theme.borderSubtle
        border.width: 1

        // Keep the item alive through the closing animation so the panel slides
        // out instead of blinking away the instant dockOpen flips false.
        property bool shown: root.dockOpen
        Connections {
            target: root
            function onDockOpenChanged() {
                if (root.dockOpen) dock.shown = true
                else closeTimer.restart()
            }
        }
        Timer { id: closeTimer; interval: Theme.durMed + 20; onTriggered: dock.shown = false }

        visible: shown
        enabled: root.dockOpen
        x: root.dockOpen ? root.width - width - Theme.dp(8) : root.width + Theme.dp(4)
        opacity: root.dockOpen ? 1.0 : 0.0

        Behavior on x { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        ScrollView {
            anchors.fill: parent
            anchors.margins: Theme.dp(12)
            contentWidth: width
            clip: true

            Column {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Row {
                    width: parent.width
                    Text {
                        text: qsTr("TERRAIN PARAMETERS")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                        color: Theme.textMuted
                    }
                }

                Flow {
                    width: parent.width
                    spacing: Theme.dp(4)

                    Repeater {
                        model: ["Platform", "Slope", "Bridge", "Island", "Stairs"]

                        Chip {
                            text: modelData
                            onClicked: root.loadPreset(modelData === "Island" ? "Floating Island" : modelData)
                        }
                    }
                }

                Rectangle { height: 1; width: parent.width; color: Theme.borderSubtle }

                // ── Selection ───────────────────────────────────────────────
                Text {
                    text: root.selectedPoint >= 0
                          ? qsTr("SELECTED VERTEX  #%1").arg(root.selectedPoint + 1)
                          : qsTr("NO VERTEX SELECTED")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                    color: root.selectedPoint >= 0 ? Theme.accentInk : Theme.textMuted
                }

                Text {
                    visible: root.selectedPoint >= 0
                    width: parent.width
                    text: root.selectedPoint >= 0
                          ? ("X " + Math.round(nodes.get(root.selectedPoint).px) +
                             "\nY " + Math.round(nodes.get(root.selectedPoint).py))
                          : ""
                    font.family: Theme.fontFamilyMono
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textPrimary
                    lineHeight: 1.3
                }

                // Nudge pad: eight directions at the snap step, so a vertex can
                // be placed exactly without fighting the grid.
                Grid {
                    visible: root.selectedPoint >= 0
                    columns: 3
                    spacing: Theme.dp(3)

                    Repeater {
                        model: [
                            { dx: -1, dy:  1, icon: "chevron-up" },
                            { dx:  0, dy:  1, icon: "arrow-up" },
                            { dx:  1, dy:  1, icon: "chevron-up" },
                            { dx: -1, dy:  0, icon: "arrow-left" },
                            { dx:  0, dy:  0, icon: "dot" },
                            { dx:  1, dy:  0, icon: "arrow-right" },
                            { dx: -1, dy: -1, icon: "chevron-down" },
                            { dx:  0, dy: -1, icon: "arrow-down" },
                            { dx:  1, dy: -1, icon: "chevron-down" }
                        ]

                        IconButton {
                            square: true
                            buttonSize: Theme.dp(30)
                            iconSize: Theme.dp(13)
                            iconName: modelData.icon
                            // The middle cell is the "duplicate this vertex"
                            // affordance — rotating the arrow grid to say
                            // something useful beats a dead centre.
                            variant: modelData.dx === 0 && modelData.dy === 0 ? "soft" : "plain"
                            tooltip: modelData.dx === 0 && modelData.dy === 0
                                     ? qsTr("Duplicate vertex")
                                     : qsTr("Nudge by %1").arg(root.snapStep > 0 ? root.snapStep : 1)
                            onClicked: {
                                var step = root.snapStep > 0 ? root.snapStep : 1
                                if (modelData.dx === 0 && modelData.dy === 0) {
                                    var p = nodes.get(root.selectedPoint)
                                    root.insertPointAt(root.selectedPoint + 1, p.px + step, p.py)
                                } else {
                                    root.nudgeSelected(modelData.dx * step, modelData.dy * step)
                                }
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(6)

                    Button {
                        width: (parent.width - Theme.dp(6)) / 2
                        text: qsTr("Insert after")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        enabled: root.selectedPoint >= 0
                        onClicked: {
                            var p = nodes.get(root.selectedPoint)
                            root.insertPointAt(root.selectedPoint + 1, p.px + 30, p.py + 30)
                        }
                    }

                    Button {
                        width: (parent.width - Theme.dp(6)) / 2
                        text: qsTr("Delete")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        enabled: root.selectedPoint >= 0 && root.pointCount > 3
                        onClicked: root.removePointAt(root.selectedPoint)
                    }
                }

                Rectangle { height: 1; width: parent.width; color: Theme.borderSubtle }

                // ── Depth extents ───────────────────────────────────────────
                Text {
                    text: qsTr("TERRAIN EXTENTS")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                    color: Theme.textMuted
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(6)
                    Text {
                        text: qsTr("Depth min")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ThemedField {
                        width: Theme.dp(72)
                        text: "" + root.minDepth
                        // onEditingFinished, not onTextChanged: writing the parsed
                        // value back while the user is mid-keystroke fought the
                        // caret ("-" parsed to the default and erased the sign).
                        onEditingFinished: root.minDepth = parseFloat(text) || 0.0
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(6)
                    Text {
                        text: qsTr("Depth max")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ThemedField {
                        width: Theme.dp(72)
                        text: "" + root.maxDepth
                        onEditingFinished: root.maxDepth = parseFloat(text) || 0.0
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(6)
                    Text {
                        text: qsTr("Extrude W")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ThemedField {
                        width: Theme.dp(72)
                        text: "" + root.surfaceWidth
                        onEditingFinished: root.surfaceWidth = parseFloat(text) || 0.0
                    }
                }

                Rectangle { height: 1; width: parent.width; color: Theme.borderSubtle }

                // ── Textures ────────────────────────────────────────────────
                Text {
                    text: qsTr("TEXTURES")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                    color: Theme.textMuted
                }

                Text { text: qsTr("Top surface"); color: Theme.textMuted; font.pixelSize: Theme.dp(Theme.fontSm) }
                ThemedField {
                    width: parent.width
                    text: root.topTexture
                    onEditingFinished: root.topTexture = text.trim()
                }

                Text { text: qsTr("Front face"); color: Theme.textMuted; font.pixelSize: Theme.dp(Theme.fontSm) }
                ThemedField {
                    width: parent.width
                    text: root.frontTexture
                    onEditingFinished: root.frontTexture = text.trim()
                }

                Rectangle { height: 1; width: parent.width; color: Theme.borderSubtle }

                Text {
                    text: qsTr("GRID")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.Bold
                    font.letterSpacing: 1.1
                    color: Theme.textMuted
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(8)

                    CheckBox {
                        id: gridCheck
                        text: qsTr("Show grid")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        checked: root.showGrid
                        onToggled: {
                            root.showGrid = checked
                            canvas.requestPaint()
                        }
                    }
                }
            }
        }
    }

    // ========================================================================
    //  Node context menu (long press)
    // ========================================================================
    Menu {
        id: nodeMenu
        property int targetIndex: -1

        MenuItem {
            text: qsTr("Insert vertex after")
            onTriggered: {
                var p = nodes.get(nodeMenu.targetIndex)
                root.insertPointAt(nodeMenu.targetIndex + 1, p.px + 30, p.py + 30)
            }
        }

        MenuItem {
            text: qsTr("Duplicate vertex")
            onTriggered: {
                var p = nodes.get(nodeMenu.targetIndex)
                root.insertPointAt(nodeMenu.targetIndex + 1, p.px + 15, p.py + 15)
            }
        }

        MenuItem {
            text: qsTr("Select")
            onTriggered: root.selectedPoint = nodeMenu.targetIndex
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Delete vertex")
            enabled: root.pointCount > 3
            onTriggered: root.removePointAt(nodeMenu.targetIndex)
        }
    }

    // ========================================================================
    //  Export
    // ========================================================================
    function doExport() {
        if (pointCount < 3) {
            toast.show(qsTr("A sheet needs at least 3 vertices"))
            return
        }
        var outPath = "/sdcard/Swordigo/mesh_" + Date.now() + ".swdm"
        if (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.currentPath)
            outPath = rubyFileModel.currentPath + "/custom_terrain.swdm"

        if (typeof rubyToolsBridge === "undefined" || !rubyToolsBridge) {
            toast.show(qsTr("Export bridge unavailable"))
            return
        }
        var ok = rubyToolsBridge.exportSwdm(outPath, root.toList(), root.minDepth, root.maxDepth,
                                            root.topTexture, root.frontTexture, root.surfaceWidth, 0.0)
        if (ok) {
            toast.show(qsTr("Exported → %1").arg(outPath))
            root.buzz(30)
        } else {
            toast.show(qsTr("Export failed"))
        }
    }

    // ========================================================================
    //  Toast
    // ========================================================================
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(14)
        height: Theme.dp(34)
        width: Math.min(toastText.implicitWidth + Theme.dp(28), root.width - Theme.dp(32))
        radius: Theme.radiusPill
        color: Theme.alpha(Theme.surface3, 0.96)
        border.color: Theme.accentInk
        border.width: 1
        opacity: 0.0
        z: 40

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - Theme.dp(24)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            font.pixelSize: Theme.dp(Theme.fontSm)
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }

        Timer {
            id: toastTimer
            interval: 2400
            onTriggered: toast.opacity = 0.0
        }

        function show(msg) {
            toastText.text = msg
            toast.opacity = 1.0
            toastTimer.restart()
        }
    }

    // ── Landscape gate — covers drafting bench if held in portrait ──────────
    OrientationGate {
        anchors.fill: parent
        z: 100
        blocking: !root.studioFits
        platformCanRotate: typeof screenOrientation !== "undefined"
                            && screenOrientation !== null
                            && screenOrientation.supported
        targetName: qsTr("Ground Mesh Studio")
        onBackRequested: root.backRequested()
    }
}
