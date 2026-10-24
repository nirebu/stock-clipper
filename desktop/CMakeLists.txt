cmake_minimum_required(VERSION 3.21)

project(stock-clipper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Multimedia)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(stock-clipper
    src/main.cpp
    src/core.cpp src/core.h
    src/media.cpp src/media.h
    src/library.cpp src/library.h
    src/render.cpp src/render.h
    src/backend.cpp src/backend.h
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(stock-clipper
    URI StockClipper
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Btn.qml
        qml/TickBox.qml
        qml/ClipRow.qml
        qml/Stage.qml
        qml/Timeline.qml
        qml/QueuePanel.qml
        qml/Sheet.qml
        qml/SettingsDialog.qml
        qml/BrowseDialog.qml
        qml/Toast.qml
)

target_link_libraries(stock-clipper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
)

qt_add_executable(stock-clipper-selftest
    tests/selftest.cpp
    src/core.cpp src/core.h
    src/media.cpp src/media.h
    src/library.cpp src/library.h
    src/render.cpp src/render.h
    src/backend.cpp src/backend.h
)
target_include_directories(stock-clipper-selftest PRIVATE src)
target_link_libraries(stock-clipper-selftest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
)

install(TARGETS stock-clipper RUNTIME DESTINATION bin)
