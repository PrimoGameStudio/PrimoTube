import QtQuick
import QtQuick.Controls

Item {
    id: theme

    // Define colors as properties for easy access
    property color backgroundColor: isDark ? "#121212" : "#ffffff"
    property color surfaceColor: isDark ? "#1e1e1e" : "#f5f5f5"
    property color textColor: isDark ? "#ffffff" : "#000000"
    property color primaryColor: "#ff0000" // YouTube Red

    // Default to dark mode for a modern media client feel
    property bool isDark: true
}
