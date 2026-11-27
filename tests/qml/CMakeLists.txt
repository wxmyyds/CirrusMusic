qt_add_executable(tst_page_stack ../unit/page_stack_test.cpp)
qt_add_qml_module(tst_page_stack
    URI Cirrus.Test.PageStack
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Components
    QML_FILES
        PageStackHarness.qml
)
target_link_libraries(tst_page_stack PRIVATE cirrus_ui_components Qt6::Quick Qt6::Test)
target_compile_definitions(tst_page_stack PRIVATE CIRRUS_QML_IMPORT_PATH="${CMAKE_BINARY_DIR}/qml")
qt_import_qml_plugins(tst_page_stack)
add_test(NAME page_stack COMMAND tst_page_stack)
set_tests_properties(page_stack PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
