set(OCAPI_CORE_SOURCES
    core/message.h
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
target_link_libraries(ocapi-core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)

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
