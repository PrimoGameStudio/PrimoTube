import QtQuick
import QtQuick.Controls

Item {
    id: theme

    // Define colors as properties for easy access
    property color backgroundColor: isDark ? "#121212" : "#ffffff"
    property color surfaceColor: isDark ? "#1e1e1e" : "#f5f5f5"
    property color textColor: isDark ? "#ffffff" : "#000000"
    property color primaryColor: omarchyTheme && omarchyTheme.accentColorValid ? Qt.color(omarchyTheme.accentColor) : "#ff0000"

    // Qt Quick Controls palette colors for themed widgets
    property color qccButton: isDark ? "#1e1e1e" : "#f5f5f5"
    property color qccButtonText: isDark ? "#ffffff" : "#000000"
    property color qccHighlight: primaryColor
    property color qccHighlightedText: "#ffffff"
    property color qccMid: isDark ? "#333333" : "#cccccc"
    property color qccShadow: isDark ? "#000000" : "#999999"

    // Default to dark mode for a modern media client feel
    property bool isDark: true
}
