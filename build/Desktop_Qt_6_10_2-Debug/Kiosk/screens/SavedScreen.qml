import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Controls.Material 6.5

Item {
    id: root
    signal done()

    Timer {
        interval: 1200
        running: true
        repeat: false
        onTriggered: root.done()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 720
        height: 260
        radius: 18
        color: "#141925"
        border.color: "#273044"
        border.width: 1

        Column {
            anchors.centerIn: parent
            spacing: 14
            width: parent.width - 40

            Text {
                text: "Saved"
                font.pixelSize: 38
                font.bold: true
                color: "#ffffff"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }

            Text {
                text: AppState.currentProduct && AppState.currentProduct.name
                      ? ("Saved: " + AppState.currentProduct.name)
                      : "Saved."
                font.pixelSize: 18
                color: "#b8c0cc"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Button {
                text: "Back to Scan"
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter

                background: Rectangle {
                    radius: 12
                    color: "#00c2ff"
                    border.color: "#2a3445"
                    border.width: 1
                }

                contentItem: Text {
                    text: parent.text
                    color: "#081018"
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.done()
            }


            Text {
                text: "Returning to scan…"
                font.pixelSize: 14
                color: "#7f8aa3"
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
            }
        }
    }
}
