pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#0b0d11"
    readonly property color panel: "#13161c"
    readonly property color panel2: "#181c23"
    readonly property color panel3: "#1f242d"
    readonly property color line: "#272d38"
    readonly property color lineSoft: "#1e232b"
    readonly property color text: "#e8ebf1"
    readonly property color muted: "#8b94a5"
    readonly property color accent: "#f5b942"
    readonly property color accentDim: "#7a5a1c"
    readonly property color ok: "#4ade80"
    readonly property color danger: "#f87171"

    readonly property int radius: 10
    readonly property int gap: 10

    readonly property string uiFont: "Inter, Cantarell, Noto Sans, sans-serif"
    readonly property string monoFont: "JetBrains Mono, Fira Code, monospace"
}
