set(OKAPI_TESTS
    tst_backendconfig
    tst_markdown
    tst_message
    tst_imagemessage
    tst_attachmentopener
    tst_filepreview
    tst_chatsession
    tst_chatsession_tools
    tst_sessiongrouper
    tst_backendregistry
    tst_backendfactory
    tst_ollamabackend
    tst_openaiapibackend
    tst_geminibackend
    tst_sseparser
    tst_chatdatabase
    tst_textextractor
    tst_mcpprotocol
    tst_mcpserverconfig
    tst_mcpregistry
    tst_mcpserverdatabase
    tst_mcpclient
    tst_mcpmanager
    tst_sttconfig
    tst_sttregistry
    tst_systemprompt
    tst_voiceinput
    tst_audiochunker
    tst_sttclient
    tst_sttstreamclient
    tst_ttsconfig
    tst_ttsregistry
    tst_shortcutregistry
    tst_uisettings
    tst_prompttemplateregistry
    tst_sentencesplitter
    tst_audioformat
    tst_ttsclient
    tst_ttsplayer
    tst_debuglog)


foreach(test_name ${OKAPI_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE okapi-core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The sidebar lives in the application target, so its test compiles the
# source directly and links against Qt Widgets.
add_executable(tst_sidebar tst_sidebar.cpp ${CMAKE_SOURCE_DIR}/src/ui/sidebar.cpp)
target_include_directories(tst_sidebar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_sidebar PRIVATE okapi-core Qt6::Test Qt6::Widgets)
add_test(NAME tst_sidebar COMMAND tst_sidebar)

# The chat view (and its delegate) also live in the application target; the
# test compiles them directly like tst_sidebar.
add_executable(tst_chatview tst_chatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatdelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp)
target_include_directories(tst_chatview PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_chatview PRIVATE okapi-core Qt6::Test Qt6::Widgets Qt6::Svg)
add_test(NAME tst_chatview COMMAND tst_chatview)

# The settings dialog lives in the application target; the test compiles it
# directly like tst_sidebar and needs the page widgets it hosts.
add_executable(tst_settingsdialog tst_settingsdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/settingsdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/backendpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/mcppage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/shortcutpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sttpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/systempromptpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ttspage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/userinterfacepage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/prompttemplatespage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp)
target_include_directories(tst_settingsdialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_settingsdialog PRIVATE okapi-core Qt6::Test Qt6::Widgets Qt6::Svg)
add_test(NAME tst_settingsdialog COMMAND tst_settingsdialog)

# MainWindow lives in the application target; the test compiles it directly
# like tst_settingsdialog and needs the UI sources it pulls in.
add_executable(tst_mainwindow tst_mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/webchatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatdelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/composer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sidebar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/backendpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/mcppage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/shortcutpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sttpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/systempromptpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ttspage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/userinterfacepage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/prompttemplatespage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/settingsdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp)
target_include_directories(tst_mainwindow PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_mainwindow PRIVATE okapi-core Qt6::Test Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Svg)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)

# The settings dialog shows themed icons with bundled SVGs from the app's
# resource file; the test needs the same resources embedded to resolve them.
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/icons/ai_chip.svg
    PROPERTIES QT_RESOURCE_ALIAS "resources/icons/ai_chip.svg")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/icons/tools.svg
    PROPERTIES QT_RESOURCE_ALIAS "resources/icons/tools.svg")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/icons/general.svg
    PROPERTIES QT_RESOURCE_ALIAS "resources/icons/general.svg")
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/icons/keyboard.svg
    PROPERTIES QT_RESOURCE_ALIAS "resources/icons/keyboard.svg")
qt_add_resources(tst_settingsdialog "okapi_resources"
    PREFIX "/"
    FILES
        ${CMAKE_SOURCE_DIR}/resources/icons/ai_chip.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/tools.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/general.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/keyboard.svg)
