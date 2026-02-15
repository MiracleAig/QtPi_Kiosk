import QtQuick 6.5
import QtQuick.Controls 6.5
import "../components" as Components

Item {
    id: root
    signal goConfirm()

    Components.BarcodeWedge {
        id: wedge
        anchors.fill: parent
        focus: true   // IMPORTANT

        onScanned: (code) => {
            code = code.trim()
            if (code.length === 12)
                code = "0" + code

            AppState.setBarcode(code)
            ProductLookup.lookup(code)
        }
    }

    // Fires every time ScanScreen becomes the current StackView item
    StackView.onActivated: {
        wedge.forceActiveFocus()
    }

    Component.onCompleted: wedge.forceActiveFocus()

    Column {
        anchors.centerIn: parent
        spacing: 18

        Text { text: "Scan"; font.pixelSize: 34 }
        Text { text: "Barcode: " + (AppState.currentBarcode || "—"); font.pixelSize: 18 }

        Button {
            text: "Focus (if scanner not working)"
            focusPolicy: Qt.NoFocus
            onClicked: wedge.forceActiveFocus()
        }

    }

    Connections {
        target: ProductLookup

        function onProductReady(product) {
            AppState.setProduct(product && Object.keys(product).length ? product : null)
            goConfirm()
        }

        function onError(message) {
            console.log("LOOKUP ERROR:", message)
        }
    }
}
