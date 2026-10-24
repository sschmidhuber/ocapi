set(OCAPI_TESTS
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
    tst_audiochunker
    tst_sttclient
    tst_sttstreamclient
    tst_ttsconfig
    tst_ttsregistry
    tst_sentencesplitter
    tst_audioformat
    tst_ttsclient)


foreach(test_name ${OCAPI_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE ocapi-core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The sidebar lives in the application target, so its test compiles the
# source directly and links against Qt Widgets.
add_executable(tst_sidebar tst_sidebar.cpp ${CMAKE_SOURCE_DIR}/src/ui/sidebar.cpp)
target_include_directories(tst_sidebar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_sidebar PRIVATE ocapi-core Qt6::Test Qt6::Widgets)
add_test(NAME tst_sidebar COMMAND tst_sidebar)

# The chat view (and its delegate) also live in the application target; the
# test compiles them directly like tst_sidebar.
add_executable(tst_chatview tst_chatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatview.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/chatdelegate.cpp)
target_include_directories(tst_chatview PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_chatview PRIVATE ocapi-core Qt6::Test Qt6::Widgets)
add_test(NAME tst_chatview COMMAND tst_chatview)
