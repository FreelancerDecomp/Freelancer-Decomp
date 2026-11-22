# PortTests: checks of the port's own platform headers on Linux and macOS -- PORT(macOS), not part
# of retail. Built only by the non-Windows build (CMakeLists.txt), with ctest.

add_executable(port_tests port_tests.cpp)
target_compile_features(port_tests PRIVATE cxx_std_20)
target_include_directories(port_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
# fl_layout.h is forced into every source, as in the Windows build.
target_compile_options(port_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       -Wall -Wextra)
add_test(NAME port_tests COMMAND port_tests)
