add_executable(zopana_m4 probe.cpp)
target_link_libraries(zopana_m4 PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_m4 PRIVATE /utf-8)
set_target_properties(zopana_m4 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
