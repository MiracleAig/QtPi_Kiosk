pragma Singleton
import QtQuick 6.5

QtObject {
    property string currentBarcode: ""
    property var    currentProduct: null
    property string lastAction: ""

    function resetToScan() {
        currentBarcode = ""
        currentProduct = null
        lastAction = ""
    }

    function setBarcode(code) {
        currentBarcode = code
        lastAction = "scanned"
    }

    function setProduct(p) {
        currentProduct = p
        lastAction = "lookup"
    }

    function markSaved() {
        lastAction = "saved"
    }
}
