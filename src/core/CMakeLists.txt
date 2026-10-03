# chiply_core: document model and Wokwi file format. Plain C++20, no Qt,
# so the CLI and the tests exercise exactly the code the editor uses.
file(READ ${PROJECT_SOURCE_DIR}/resources/parts.json CHIPLY_PARTS_JSON)
configure_file(PartsData.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/PartsData.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/parts.json)

add_library(chiply_core STATIC
  Document.cpp
  Edit.cpp
  Geometry.cpp
  IdGen.cpp
  Netlist.cpp
  Drc.cpp
  PartLibrary.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/PartsData.cpp
  JsonFormat.cpp
  WirePath.cpp
  WokwiJson.cpp
)
target_include_directories(chiply_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(chiply_core PUBLIC nlohmann_json::nlohmann_json)
