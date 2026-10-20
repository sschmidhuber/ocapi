set(OCAPI_TESTS
    tst_backendconfig
    tst_message
    tst_imagemessage
    tst_chatsession
    tst_chatsession_tools
    tst_backendregistry
    tst_backendfactory
    tst_sseparser
    tst_chatdatabase
    tst_textextractor
    tst_mcpprotocol
    tst_mcpserverconfig
    tst_mcpregistry
    tst_mcpserverdatabase
    tst_mcpclient
    tst_mcpmanager)


foreach(test_name ${OCAPI_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE ocapi-core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
