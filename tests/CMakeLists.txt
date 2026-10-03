FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(chiply_tests
  test_json_format.cpp
  test_wire_path.cpp
  test_idgen.cpp
  test_wokwi_json.cpp
  test_part_library.cpp
)
target_link_libraries(chiply_tests PRIVATE chiply_core Catch2::Catch2WithMain)
target_compile_definitions(chiply_tests PRIVATE
  CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")

include(Catch)
catch_discover_tests(chiply_tests)
