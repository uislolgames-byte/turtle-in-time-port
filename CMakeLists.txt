cmake_minimum_required(VERSION 3.16)
project(tmnt_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TMNT_TOOLS rominfo disasm flow hwscan vectors entrydump callscan callgraph cfg lift exec)
set(TMNT_TEST_EXECUTABLES)
foreach(tool IN LISTS TMNT_TOOLS)
  add_executable(tmnt_${tool} src/${tool}.cpp)
  if(MSVC)
    target_compile_options(tmnt_${tool} PRIVATE /W4)
  else()
    target_compile_options(tmnt_${tool} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  list(APPEND TMNT_TEST_EXECUTABLES "${tool}=$<TARGET_FILE:tmnt_${tool}>")
endforeach()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME tools_smoke COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.py" ${TMNT_TEST_EXECUTABLES})
  set_tests_properties(tools_smoke PROPERTIES TIMEOUT 30)
endif()

if(WIN32)
  add_executable(tmnt_native WIN32 src/native_windows.cpp)
  target_link_libraries(tmnt_native PRIVATE comdlg32 user32 gdi32)
endif()

add_executable(tmnt_cpuprobe src/cpuprobe.cpp)
if(MSVC)
  target_compile_options(tmnt_cpuprobe PRIVATE /W4)
else()
  target_compile_options(tmnt_cpuprobe PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Python runs at build time; the generated executable only needs C++ at runtime.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(RECOMPILER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recompiler")
set(RECOMPILER_RUNNERS)
foreach(fixture synthetic branches loop boundaries logic)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${fixture}.cpp" "${CMAKE_CURRENT_BINARY_DIR}/${fixture}.bin"
    COMMAND "${Python3_EXECUTABLE}" "${RECOMPILER_DIR}/fixture.py"
      "${CMAKE_CURRENT_BINARY_DIR}/${fixture}.bin" "${CMAKE_CURRENT_BINARY_DIR}/${fixture}.cpp" "${fixture}"
    DEPENDS "${RECOMPILER_DIR}/fixture.py" "${RECOMPILER_DIR}/generate.py"
    VERBATIM)
  add_executable(tmnt_recompiler_${fixture} recompiler/runner.cpp "${CMAKE_CURRENT_BINARY_DIR}/${fixture}.cpp")
  target_include_directories(tmnt_recompiler_${fixture} PRIVATE "${RECOMPILER_DIR}")
  list(APPEND RECOMPILER_RUNNERS "${fixture}=$<TARGET_FILE:tmnt_recompiler_${fixture}>")
endforeach()
if(BUILD_TESTING)
  add_test(NAME recompiler_equivalence COMMAND "${Python3_EXECUTABLE}"
    "${RECOMPILER_DIR}/test_recompiler.py" ${RECOMPILER_RUNNERS})
  set_tests_properties(recompiler_equivalence PROPERTIES TIMEOUT 60)
endif()
if(BUILD_TESTING)
  add_test(NAME cpu_probe_synthetic COMMAND "$<TARGET_FILE:tmnt_cpuprobe>" --self-test)
endif()
