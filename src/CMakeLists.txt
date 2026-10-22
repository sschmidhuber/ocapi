find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(POPPLER_CPP QUIET poppler-cpp)
    pkg_check_modules(TESSERACT QUIET tesseract)
    pkg_check_modules(LIBARCHIVE QUIET libarchive)
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
if(LIBARCHIVE_FOUND)
    message(STATUS "libarchive found: MS Office document extraction enabled")
else()
    message(WARNING "libarchive not found: MS Office support disabled")
endif()

set(OCAPI_CORE_SOURCES
    core/message.h
    core/imagemessage.h
    core/imagemessage.cpp
    core/markdown.h
    core/markdown.cpp
    core/textextractor.h
    core/textextractor.cpp
    core/chatsession.h
    core/chatsession.cpp
    core/backendconfig.h
    core/backendconfig.cpp
    core/sttconfig.h
    core/sttconfig.cpp
    core/sttregistry.h
    core/sttregistry.cpp
    core/ttsconfig.h
    core/ttsconfig.cpp
    core/ttsregistry.h
    core/ttsregistry.cpp
    core/sentencesplitter.h
    core/sentencesplitter.cpp
    core/audioformat.h
    core/audioformat.cpp
    core/ttsplayer.h
    core/ttsplayer.cpp
    core/audiochunker.h
    core/audiochunker.cpp
    core/audiorecorder.h
    core/audiorecorder.cpp
    core/voiceinput.h
    core/voiceinput.cpp
    core/backendregistry.h
    core/backendregistry.cpp
    core/mcpprotocol.h
    core/mcpserverconfig.h
    core/mcpserverconfig.cpp
    core/mcpregistry.h
    core/mcpregistry.cpp
    core/mcpmanager.h
    core/mcpmanager.cpp
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
    backends/mcpclient.h
    backends/mcpclient.cpp
    backends/sttclient.h
    backends/sttclient.cpp
    backends/sttstreamclient.h
    backends/sttstreamclient.cpp
    backends/ttsclient.h
    backends/ttsclient.cpp
)

add_library(ocapi-core STATIC ${OCAPI_CORE_SOURCES})
target_include_directories(ocapi-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ocapi-core PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Gui Qt6::Multimedia Qt6::WebSockets)

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
if(LIBARCHIVE_FOUND)
    target_compile_definitions(ocapi-core PUBLIC OCAPI_HAVE_LIBARCHIVE=1)
    target_include_directories(ocapi-core PUBLIC ${LIBARCHIVE_INCLUDE_DIRS})
    target_link_directories(ocapi-core PUBLIC ${LIBARCHIVE_LIBRARY_DIRS})
    target_link_libraries(ocapi-core PUBLIC ${LIBARCHIVE_LIBRARIES})
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
    ui/backendpage.h
    ui/backendpage.cpp
    ui/mcppage.h
    ui/mcppage.cpp
    ui/sttpage.h
    ui/sttpage.cpp
    ui/ttspage.h
    ui/ttspage.cpp
    ui/settingsdialog.h
    ui/settingsdialog.cpp
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
        ../resources/icons/microphone.svg
        ../resources/icons/attach.svg
)
