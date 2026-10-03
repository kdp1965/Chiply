# chiply_core: document model and Wokwi file format. Plain C++20, no Qt,
# so the CLI and the tests exercise exactly the code the editor uses.
add_library(chiply_core STATIC
  Document.cpp
  IdGen.cpp
  JsonFormat.cpp
  WirePath.cpp
  WokwiJson.cpp
)
target_include_directories(chiply_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(chiply_core PUBLIC nlohmann_json::nlohmann_json)
