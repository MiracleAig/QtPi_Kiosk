import QtQuick 6.5
import QtQuick.Controls 6.5
import "../components" as Components

Item {
    signal done()

    Timer {
        interval: 1200
        running: true
        repeat: false
        onTriggered: done()
    }

    Column {
        anchors.centerIn: parent
        spacing: 14

        Text { text: "Saved"; font.pixelSize: 38 }
        Text {
            text: AppState.currentProduct ? ("Saved: " + AppState.currentProduct.name) : "Saved."
            font.pixelSize: 18
        }
        Button { text: "Back to Scan"; onClicked: done() }
    }

     Component.onCompleted: wedge.forceActiveFocus()
}
