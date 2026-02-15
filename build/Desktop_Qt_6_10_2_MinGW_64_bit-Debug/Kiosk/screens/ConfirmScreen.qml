import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: root
    signal goSaved()
    signal goBack()

    property var p: AppState.currentProduct

    Column {
        id: layout
        anchors.centerIn: parent
        spacing: 14
        width: 720   // keeps things centered nicely on 800x480

        Text {
            text: "Confirm"
            font.pixelSize: 34
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        Text {
            text: "Barcode: " + (AppState.currentBarcode || "—")
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        // Info card centered
        Rectangle {
            width: parent.width
            height: 180
            radius: 12
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14

                // Left: product image
                Rectangle {
                    width: 150
                    height: parent.height
                    radius: 10
                    border.width: 1
                    clip: true

                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        source: (p && p.image) ? p.image : ""
                        asynchronous: true
                        cache: true
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !(p && p.image)
                        text: "No image"
                        font.pixelSize: 14
                    }
                }

                // Right: product info
                Column {
                    width: parent.width - 150 - 14
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: p ? (p.name + (p.size ? " (" + p.size + ")" : "")) : "Product not found"
                        font.pixelSize: 22
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    Text {
                        text: p ? ("Brand: " + (p.brand || "—")) : "Try rescanning."
                        font.pixelSize: 16
                        width: parent.width
                    }

                    Text {
                        text: "Barcode: " + (AppState.currentBarcode || "—")
                        font.pixelSize: 16
                        width: parent.width
                    }
                }
            }
        }

        // Buttons (centered and visible)
        Row {
            width: parent.width
            spacing: 16

            Button {
                text: "Back"
                onClicked: goBack()
            }

            Item { width: 1; height: 1; } // spacer (works even without Layouts)

            Button {
                text: "Confirm And Save"
                enabled: !!p
                onClicked: goSaved()
            }
        }
    }
}
