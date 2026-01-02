cmake_minimum_required(VERSION 3.19)
project(OSU_Quick_Reader LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(OSU_Quick_Reader
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    structs.h
    gamewidget.h
    gamewidget.cpp
    settingsdialog.ui
    settingsdialog.h
    settingsdialog.cpp
)

target_link_libraries(OSU_Quick_Reader
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::OpenGLWidgets
        Qt::Multimedia
)

include(GNUInstallDirs)

install(TARGETS OSU_Quick_Reader
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET OSU_Quick_Reader
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
