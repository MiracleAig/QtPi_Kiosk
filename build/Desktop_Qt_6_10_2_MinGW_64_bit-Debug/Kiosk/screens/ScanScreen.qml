import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Controls.Material 6.5
import KioskBackend 1.0
import "../components" as Components

Item {
    id: root
    signal goConfirm(var product)

    ProductLookup { id: productLookup }

    Components.BarcodeWedge {
        id: wedge
        anchors.fill: parent
        focus: true

        onScanned: (code) => {
            code = code.trim()
            if (code.length === 12)
                code = "0" + code

            AppState.setBarcode(code)

            // OFFLINE-FIRST: check local cache first
            const cached = inventoryManager.findCachedProduct(code)
            if (cached && cached.barcode) {
                AppState.currentProduct = cached
                root.goConfirm(cached)
                return
            }

            // Not cached -> network lookup
            productLookup.lookup(code)
        }

    }

    StackView.onActivated: wedge.forceActiveFocus()
    Component.onCompleted: wedge.forceActiveFocus()

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 720
        height: 260
        radius: 18
        color: "#141925"
        border.color: "#273044"
        border.width: 1

        Column {
            id: content
            anchors.centerIn: parent
            width: card.width - 80
            spacing: 18

            Text {
                text: "Scan"
                font.pixelSize: 38
                font.bold: true
                color: "#ffffff"
                horizontalAlignment: Text.AlignHCenter
                width: content.width
            }

            Text {
                text: "Barcode: " + (AppState.currentBarcode || "—")
                font.pixelSize: 18
                color: "#b8c0cc"
                horizontalAlignment: Text.AlignHCenter
                width: content.width
                wrapMode: Text.WordWrap
            }

            Button {
                id: focusButton
                text: "Focus (if scanner not working)"
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
                focusPolicy: Qt.NoFocus

                background: Rectangle {
                    radius: 12
                    color: "#00c2ff"
                    border.color: "#2a3445"
                    border.width: 1
                }

                contentItem: Text {
                    text: focusButton.text
                    color: "#081018"
                    font: focusButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: wedge.forceActiveFocus()
            }

            Text {
                text: "Ready to scan"
                font.pixelSize: 14
                color: "#7f8aa3"
                horizontalAlignment: Text.AlignHCenter
                width: content.width
            }
        }
    }

    Connections {
        target: productLookup

        function onProductReady(product) {
            inventoryManager.cacheProduct(product)   // <--- add this
            AppState.currentProduct = product
            root.goConfirm(product)
        }


        function onError(message) {
            console.log("LOOKUP ERROR:", message)
        }
    }
}
