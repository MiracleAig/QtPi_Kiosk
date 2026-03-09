import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Controls.Material 6.5

Item {
    id: root
    signal goSaved()
    signal goBack()

    property var product: ({})

    Column {
        id: layout
        anchors.centerIn: parent
        spacing: 14
        width: 720

        Text {
            text: "Confirm"
            font.pixelSize: 34
            font.bold: true
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        Text {
            text: "Barcode: " + (product.barcode || AppState.currentBarcode || "—")
            font.pixelSize: 18
            color: "#b8c0cc"
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        Rectangle {
            width: parent.width
            height: 180
            radius: 14
            color: "#101522"
            border.color: "#273044"
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14

                Rectangle {
                    width: 150
                    height: parent.height
                    radius: 12
                    color: "#0b0f18"
                    border.color: "#273044"
                    border.width: 1
                    clip: true

                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit

                        // Prefer offline local image if available
                        source: (product.image_local && product.image_local.length > 0)
                                ? ("file:///" + product.image_local)
                                : (product.image ? product.image : "")

                        asynchronous: true
                        cache: true
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !product.image_local && !product.image
                        text: "No image"
                        font.pixelSize: 14
                        color: "#7f8aa3"
                    }
                }

                Column {
                    width: parent.width - 150 - 14
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: product.name
                              ? (product.name + (product.size ? " (" + product.size + ")" : ""))
                              : "Product not found"
                        font.pixelSize: 22
                        font.bold: true
                        color: "#ffffff"
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    Text {
                        text: "Brand: " + (product.brand || "—")
                        font.pixelSize: 16
                        color: "#b8c0cc"
                        width: parent.width
                    }

                    Text {
                        text: "Barcode: " + (product.barcode || AppState.currentBarcode || "—")
                        font.pixelSize: 16
                        color: "#b8c0cc"
                        width: parent.width
                    }
                }
            }
        }

        Row {
            width: parent.width
            spacing: 16

            Button {
                text: "Back"
                onClicked: root.goBack()
            }

            Item { width: 1; height: 1 }

            Button {
                text: "Confirm and Save"
                font.bold: true

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

                onClicked: {
                    // Optional: ensure cache is written even if product came from inventory fallback
                    inventoryManager.cacheProduct(product)

                    const ok = inventoryManager.insertProduct(product)
                    if (ok) root.goSaved()
                    else console.log("Save failed")
                }
            }
        }

        Connections {
            target: inventoryManager
            function onErrorOccurred(message) {
                console.log("DB error:", message)
            }
        }
    }
}
