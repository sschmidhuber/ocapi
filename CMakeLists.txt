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

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Sql Multimedia WebSockets)

option(OCAPI_BUILD_TESTS "Build the unit tests" ON)
option(OCAPI_ENABLE_CLANG_TIDY "Run clang-tidy on compilation" OFF)

if(OCAPI_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found, static analysis disabled")
    endif()
endif()

add_subdirectory(src)

if(OCAPI_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS ocapi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/dev.sschmidhuber.ocapi.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/ocapi.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/linux/dev.sschmidhuber.ocapi.appdata.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

if(WIN32)
    set(CPACK_PACKAGE_NAME "ocapi")
    set(CPACK_PACKAGE_VENDOR "sschmidhuber")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "A desktop chat client for local and cloud LLM inference backends")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "ocapi")
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
    set(CPACK_NSIS_DISPLAY_NAME "ocapi")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/sschmidhuber/ocapi")
    set(CPACK_NSIS_CONTACT "https://github.com/sschmidhuber/ocapi")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_WIX_UPGRADE_GUID "3941672F-D06E-44CB-938F-F75357BBBDDC")
    set(CPACK_WIX_PRODUCT_ICON ${CMAKE_SOURCE_DIR}/resources/icons/ocapi.ico)
    include(CPack)
endif()
