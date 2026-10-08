# The engine in the browser (PLAN.md 13): a benchmark module for wasm/index.html.
add_executable(chiply_bench bench_api.cpp)
target_link_libraries(chiply_bench PRIVATE chiply_sim)
set_target_properties(chiply_bench PROPERTIES OUTPUT_NAME chiply_bench SUFFIX .js)
target_link_options(chiply_bench PRIVATE
  -sMODULARIZE=1 -sEXPORT_NAME=createChiplyBench -sENVIRONMENT=web,worker
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=4MB
  "-sEXPORTED_FUNCTIONS=['_chiply_bench','_chiply_free','_malloc','_free']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']")
