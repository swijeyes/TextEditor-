# Fuzz targets for the code that reads untrusted input. With a Clang that
# has libFuzzer (e.g. Homebrew's llvm), they are real libFuzzer targets:
#   ./fuzz_textcodec -max_total_time=600 corpus/
# Otherwise they link a small standalone driver that runs random and
# mutated inputs (see standalone_main.cpp), so they still build anywhere.

# TEXTEDITOR_HAVE_LIBFUZZER is checked in the top-level CMakeLists.txt.

function(texteditor_fuzzer name)
    add_executable(fuzz_${name} fuzz_${name}.cpp)
    target_link_libraries(fuzz_${name} PRIVATE ${ARGN})
    if(TEXTEDITOR_HAVE_LIBFUZZER)
        target_compile_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    else()
        target_sources(fuzz_${name} PRIVATE standalone_main.cpp)
    endif()
    texteditor_harden(fuzz_${name})
endfunction()

texteditor_fuzzer(textcodec texteditor_core)
texteditor_fuzzer(location texteditor_core)
texteditor_fuzzer(highlighter texteditor_ui)
texteditor_fuzzer(latexhtml texteditor_core)
