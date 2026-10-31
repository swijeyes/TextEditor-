# Qt Test unit tests; run them with ctest.

function(texteditor_test name)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE ${ARGN} Qt6::Test)
    texteditor_harden(tst_${name})
    add_test(NAME ${name} COMMAND tst_${name})
endfunction()

texteditor_test(textcodec texteditor_core)
texteditor_test(location texteditor_core)
texteditor_test(localfilesystem texteditor_core)
texteditor_test(latexhtml texteditor_core)
# Runs installed TeX engines (skipped without one).
texteditor_test(texcompiler texteditor_core)
set_tests_properties(texcompiler PROPERTIES TIMEOUT 900)
texteditor_test(classicstyle texteditor_ui)
set_tests_properties(classicstyle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
texteditor_test(highlighter texteditor_ui)
set_tests_properties(highlighter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The window, without a screen.
texteditor_test(mainwindow texteditor_ui)
set_tests_properties(mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(TEXTEDITOR_SFTP)
    # Skipped unless the TEXTEDITOR_TEST_* variables describe test servers.
    texteditor_test(sftp texteditor_core)

    # Starts throwaway OpenSSH and hostile servers, then runs tst_sftp.
    if(NOT WIN32)
        set(TEXTEDITOR_TEST_PYTHON python3 CACHE STRING "Python with paramiko, for the hostile SFTP server")
        add_test(NAME sftp_integration
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/sftp/run-integration-tests.sh
                         ${CMAKE_BINARY_DIR} ${TEXTEDITOR_TEST_PYTHON})
        set_tests_properties(sftp_integration PROPERTIES LABELS integration TIMEOUT 600)
    endif()
endif()
