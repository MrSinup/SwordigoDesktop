pragma Singleton
// ============================================================================
// Theme.qml — the single design system for Ruby GG Mobile.
//
// PALETTE PROVENANCE
//   Every colour below is promoted from the QSS that was previously inlined in
//   each legacy QWidget (mobile_asset_browser.cpp, mobile_code_editor.cpp,
//   mobile_dpad_widget.cpp, mobile_hierarchy_drawer.cpp). Nothing was invented:
//   the surface ramp, hairlines, accents and semantic type colours are the same
//   values the shipped Widget UI used, now expressed once instead of repeated
//   as hex literals in four files.
//
//   The semantic type colours are carried forward VERBATIM from
//   MobileAssetBrowser::populate_files(), so a .scene file is still amber, an
//   .scl is still green, a .pod/.glb is still blue, a .zip/.apk is still
//   yellow, a .lua is still violet and a texture is still pink. Only the icon
//   rendering changed (emoji -> vector paths); the colour meaning did not.
//
// PROPERTY NAMES ARE A PUBLIC CONTRACT
//   Every view/component in qml/ reads these names. Renaming or removing one
//   breaks the build of the whole UI, so treat additions as safe and renames
//   as a coordinated multi-file change.
// ============================================================================
import QtQuick

QtObject {
    id: theme

    // ── Responsive scale ───────────────────────────────────────────────────
    // Set once from Main.qml based on the actual screen width, so a 320dp
    // phone and a 480dp tablet both get sane touch targets. Every metric below
    // is a design-unit value that callers pass through dp().
    property real scaleFactor: 1.0

    /// Design units -> device-independent pixels.
    function dp(n) { return Math.round(n * scaleFactor * 100) / 100 }

    /// Type-safe translucent variant of a theme colour.
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // ── Surfaces (Dark Studio elevation ramp) ─────────────────────────────
    readonly property color surface0:      "#121316"  // app canvas / viewport backdrop
    readonly property color surface1:      "#181A1F"  // panels / cards / rows
    readonly property color surface2:      "#21242B"  // raised rows, inputs, bars
    readonly property color surface3:      "#282C34"  // chips, active tier
    readonly property color surfaceSunken: "#121316"  // editor / viewport well
    readonly property color surfaceGlass:  "#E6181A1F" // 90% translucent HUD panel

    // ── Hairlines ──────────────────────────────────────────────────────────
    readonly property color border:        "#2D313B"  // subtle studio divider
    readonly property color borderSubtle:  "#21242B"  // minimal divider
    readonly property color borderStrong:  "#3E4451"  // elevated border
    readonly property color borderFocus:   "#E06C75"  // Ruby crimson focus ring
    readonly property color edgeHighlight: "transparent" // no fake AI slop line

    // ── Ruby GG Brand Accent (Ruby Crimson / Coral Red) ────────────────────
    readonly property color accentStart: "#D3515B"
    readonly property color accentEnd:   "#E06C75"
    readonly property color accentSoft:  "#351E24"
    readonly property color accentDeep:  "#5C1D24"
    readonly property color accentInk:   "#F07178"
    readonly property color onAccent:    "#FFFFFF"

    // ── Typography (One Dark / Studio Contrast) ────────────────────────────
    readonly property color textPrimary:   "#E5E9F0"
    readonly property color textSecondary: "#9AA2B1"
    readonly property color textMuted:     "#5C6370"
    readonly property color textDisabled:  "#4B5263"

    // ── Status ─────────────────────────────────────────────────────────────
    readonly property color colorSuccess:       "#98C379"
    readonly property color colorSuccessDeep:   "#233821"
    readonly property color colorSuccessBright: "#A9DC76"
    readonly property color colorWarning:       "#E5C07B"
    readonly property color colorAmberDeep:     "#4D3A18"
    readonly property color colorError:         "#E06C75"
    readonly property color colorErrorBright:   "#EF7D86"
    readonly property color colorDanger:        colorError

    // ── Semantic Asset Types (Desktop Studio Colors) ───────────────────────
    readonly property color colorFolder:  "#D97706"  // amber / gold
    readonly property color colorScene:   "#F59E0B"  // terrain gold
    readonly property color colorScl:     "#10B981"  // emerald catalog
    readonly property color colorModel:   "#38BDF8"  // cyan wireframe
    readonly property color colorArchive: "#F5D75F"  // yellow archive
    readonly property color colorCode:    "#60A5FA"  // blue / indigo script
    readonly property color colorTexture: "#A855F7"  // PowerVR violet
    readonly property color colorAudio:   "#EC4899"  // audio pink / rose
    readonly property color colorGeneric: "#94A3B8"

    // ── Radii ──────────────────────────────────────────────────────────────
    readonly property int radiusXs:    4
    readonly property int radiusSm:    8
    readonly property int radiusMd:    10
    readonly property int radiusCard:  14
    readonly property int radiusLg:    16
    readonly property int radiusSheet: 20
    readonly property int radiusPill:  999

    // ── Spacing (4pt grid) ─────────────────────────────────────────────────
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24

    // ── Layout metrics ─────────────────────────────────────────────────────
    readonly property int rowHeight:     56   // list row (was QListWidget::item 50px)
    readonly property int rowHeightWide: 64
    readonly property int minTouchTarget: 48  // Material minimum
    readonly property int topBarHeight:   56
    readonly property int navBarHeight:   66
    readonly property int paddingScreen:  16
    readonly property int iconButtonSize: 38

    // ── Icon sizes ─────────────────────────────────────────────────────────
    readonly property int iconXs: 14
    readonly property int iconSm: 18
    readonly property int iconMd: 22
    readonly property int iconLg: 26
    readonly property int iconXl: 34

    // ── Type scale ─────────────────────────────────────────────────────────
    readonly property int fontXs:    10
    readonly property int fontSm:    11
    readonly property int fontMd:    13
    readonly property int fontLg:    14
    readonly property int fontXl:    17
    readonly property int fontTitle: 20
    readonly property string monoFamily: "monospace"
    readonly property string fontFamilyMono: monoFamily
    readonly property string fontFamily: "sans-serif"

    // ── Motion ─────────────────────────────────────────────────────────────
    readonly property int durFast: 120
    readonly property int durMed:  180
    readonly property int durSlow: 260

    // ── File-type helpers ──────────────────────────────────────────────────

    /// Canonical folder/scene/scl/model/archive/code/texture type for a
    /// file name, extension or already-normalised type string.
    function normalizeType(typeStr) {
        var t = (typeStr || "").toLowerCase()
        if (t.indexOf(".") >= 0) t = t.substring(t.lastIndexOf(".") + 1)

        if (t === "dir" || t === "folder") return "folder"
        if (t === "scene") return "scene"
        if (t === "scl") return "scl"
        if (t === "model" || t === "pod" || t === "glb" || t === "fbx" || t === "obj") return "model"
        if (t === "archive" || t === "zip" || t === "apk" || t === "tar" || t === "gz") return "archive"
        if (t === "code" || t === "lua" || t === "filerift" || t === "txt" || t === "json" || t === "boulder") return "code"
        if (t === "texture" || t === "pvr" || t === "tex" || t === "png" || t === "jpg" || t === "jpeg") return "texture"
        if (t === "audio" || t === "wav" || t === "ogg" || t === "mp3") return "audio"
        return "generic"
    }

    function colorForType(typeStr) {
        switch (normalizeType(typeStr)) {
        case "folder":  return colorFolder
        case "scene":   return colorScene
        case "scl":     return colorScl
        case "model":   return colorModel
        case "archive": return colorArchive
        case "code":    return colorCode
        case "texture": return colorTexture
        case "audio":   return colorAudio
        default:        return colorGeneric
        }
    }

    /// Icon name for a file type — the 1:1 semantic successor of the old
    /// text glyphs (map, layers, cube, archive, braces, image).
    function iconForType(typeStr) {
        switch (normalizeType(typeStr)) {
        case "folder":  return "folder"
        case "scene":   return "map"
        case "scl":     return "layers"
        case "model":   return "cube"
        case "archive": return "archive"
        case "code":    return "braces"
        case "texture": return "image"
        case "audio":   return "volume"
        default:        return "file"
        }
    }

    /// Soft tinted plate behind a file-type icon.
    function plateForType(typeStr) { return alpha(colorForType(typeStr), 0.14) }
}
