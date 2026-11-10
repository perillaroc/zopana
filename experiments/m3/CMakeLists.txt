add_executable(zopana_m3 probe.cpp)
target_link_libraries(zopana_m3 PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_m3 PRIVATE /utf-8)
set_target_properties(zopana_m3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
