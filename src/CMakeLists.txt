find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(POPPLER_CPP QUIET poppler-cpp)
    pkg_check_modules(TESSERACT QUIET tesseract)
endif()
if(POPPLER_CPP_FOUND)
    message(STATUS "Poppler found: PDF text extraction enabled")
else()
    message(WARNING "poppler-cpp not found: PDF support disabled")
endif()
if(TESSERACT_FOUND)
    message(STATUS "Tesseract found: OCR text extraction enabled")
else()
    message(WARNING "tesseract not found: OCR support disabled")
endif()

set(OCAPI_CORE_SOURCES
    core/message.h
    core/imagemessage.h
    core/imagemessage.cpp
    core/textextractor.h
    core/textextractor.cpp
    core/chatsession.h
    core/chatsession.cpp
    core/backendconfig.h
    core/backendconfig.cpp
    core/backendregistry.h
    core/backendregistry.cpp
    storage/chatdatabase.h
    storage/chatdatabase.cpp

    backends/ibackend.h
    backends/sseparser.h
    backends/sseparser.cpp
    backends/sseclient.h
    backends/sseclient.cpp
    backends/ollamabackend.h
    backends/ollamabackend.cpp
    backends/openaiapibackend.h
    backends/openaiapibackend.cpp
    backends/anthropicbackend.h
    backends/anthropicbackend.cpp
    backends/geminibackend.h
    backends/geminibackend.cpp
    backends/backendfactory.h
    backends/backendfactory.cpp
)

add_library(ocapi-core STATIC ${OCAPI_CORE_SOURCES})
target_include_directories(ocapi-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ocapi-core PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Gui)

if(POPPLER_CPP_FOUND)
    target_compile_definitions(ocapi-core PUBLIC OCAPI_HAVE_POPPLER=1)
    target_include_directories(ocapi-core PUBLIC ${POPPLER_CPP_INCLUDE_DIRS})
    target_link_directories(ocapi-core PUBLIC ${POPPLER_CPP_LIBRARY_DIRS})
    target_link_libraries(ocapi-core PUBLIC ${POPPLER_CPP_LIBRARIES})
endif()
if(TESSERACT_FOUND)
    target_compile_definitions(ocapi-core PUBLIC OCAPI_HAVE_TESSERACT=1)
    target_include_directories(ocapi-core PUBLIC ${TESSERACT_INCLUDE_DIRS})
    target_link_directories(ocapi-core PUBLIC ${TESSERACT_LIBRARY_DIRS})
    target_link_libraries(ocapi-core PUBLIC ${TESSERACT_LIBRARIES})
endif()

set(OCAPI_UI_SOURCES
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/chatview.h
    ui/chatview.cpp
    ui/chatdelegate.h
    ui/chatdelegate.cpp
    ui/composer.h
    ui/composer.cpp
    ui/sidebar.h
    ui/sidebar.cpp
    ui/backenddialog.h
    ui/backenddialog.cpp
)

qt_add_executable(ocapi
    main.cpp
    ${OCAPI_UI_SOURCES}
)

target_link_libraries(ocapi PRIVATE ocapi-core Qt6::Widgets)

qt_add_resources(ocapi "ocapi_resources"
    PREFIX "/"
    FILES
        ../resources/icons/ocapi.svg
)
