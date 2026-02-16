import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Controls.Material 6.5
import "screens" as Screens

ApplicationWindow {
    id: win
    visible: true
    width: 800
    height: 480
    title: "FridgeSenseUI 2.0"

    // Dark mode styling
    Material.theme: Material.Dark
    Material.accent: Material.Cyan
    Material.primary: Material.BlueGrey

    // App background
    color: "#0f1115"

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0f1115" }
            GradientStop { position: 1.0; color: "#151b28" }
        }
    }

    StackView {
        id: nav
        anchors.fill: parent

        initialItem: Screens.ScanScreen {
            onGoConfirm: (product) => {
                nav.push(confirmComponent, { product: product })
            }
        }
    }

    Component {
        id: confirmComponent
        Screens.ConfirmScreen {
            onGoSaved: nav.push(savedComponent)
            onGoBack: nav.pop()
        }
    }

    Component.onCompleted: {
        inventoryManager.loadInventory()
    }

    Component {
        id: savedComponent
        Screens.SavedScreen {
            onDone: {
                nav.pop(null)
                Qt.callLater(() => {
                    AppState.resetToScan()
                })
            }
        }
    }
}
