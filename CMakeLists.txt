cmake_minimum_required(VERSION 3.21)

project(ocapi
    VERSION 0.1.0
    DESCRIPTION "A desktop chat client for local and cloud LLM inference backends"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

add_subdirectory(src)

include(GNUInstallDirs)
install(TARGETS ocapi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/dev.sschmidhuber.ocapi.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/ocapi.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
