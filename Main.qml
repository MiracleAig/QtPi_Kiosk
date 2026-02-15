import QtQuick 6.5
import QtQuick.Controls 6.5


import "screens" as Screens

ApplicationWindow {
    id: win
    visible: true
    width: 800
    height: 480
    title: "FridgeSenseUI 2.0"

    StackView {
        id: nav
        anchors.fill: parent

        initialItem: Screens.ScanScreen {
            onGoConfirm: nav.push(confirmComponent)
        }
    }

    Component {
        id: confirmComponent
        Screens.ConfirmScreen {
            onGoSaved: nav.push(savedComponent)
            onGoBack: nav.pop()
        }
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
