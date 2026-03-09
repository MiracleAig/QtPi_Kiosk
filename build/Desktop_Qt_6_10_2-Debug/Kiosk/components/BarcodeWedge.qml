import QtQuick 6.5

FocusScope {
    id: root
    signal scanned(string code)

    focus: true

    property string buffer: ""

    Keys.onPressed: (e) => {
        // Many scanners send Return/Enter at end
        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
            if (buffer.length > 0) {
                scanned(buffer)
                buffer = ""
            }
            e.accepted = true
            return
        }

        // Collect digits (common barcodes)
        if (e.text && e.text.length === 1) {
            const ch = e.text
            if (ch >= "0" && ch <= "9") {
                buffer += ch
                e.accepted = true
                return
            }
        }

        // Optional: clear on Escape
        if (e.key === Qt.Key_Escape) {
            buffer = ""
            e.accepted = true
        }
    }

    // Keep focus even after taps
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton   // don't eat touches/clicks
        onPressed: root.forceActiveFocus()
    }
}
