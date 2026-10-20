set(OCAPI_SOURCES
    main.cpp

    core/message.h
    core/chatsession.h
    core/chatsession.cpp
    core/backendconfig.h
    core/backendconfig.cpp
    core/backendregistry.h
    core/backendregistry.cpp

    backends/ibackend.h
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

qt_add_executable(ocapi ${OCAPI_SOURCES})

target_include_directories(ocapi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ocapi PRIVATE Qt6::Widgets Qt6::Network)

qt_add_resources(ocapi "ocapi_resources"
    PREFIX "/"
    FILES
        ../resources/icons/ocapi.svg
)
