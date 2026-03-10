cmake_minimum_required(VERSION 3.16)

project(Kiosk VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Sql SerialPort)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appKiosk
    main.cpp
    productlookup.cpp
    productlookup.h
    webuiserver.cpp
    webuiserver.h
    inventorymanager.h
    inventorymanager.cpp
)

# Embed web UI files into the binary
qt_add_resources(appKiosk WEBUI_RESOURCES webui.qrc)

set_source_files_properties(AppState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appKiosk
    URI Kiosk
    QML_FILES
        Main.qml
        AppState.qml
        MockProducts.js
        components/BarcodeWedge.qml
        screens/ScanScreen.qml
        screens/ConfirmScreen.qml
        screens/SavedScreen.qml
)

qt_add_resources(appKiosk "webui"
  PREFIX "/web"
  FILES
    web/login.html
    web/dashboard.html
    web/styles.css
    web/app.js
)

target_link_libraries(appKiosk
    PRIVATE Qt6::Quick Qt6::Network Qt6::Sql Qt6::SerialPort
)
