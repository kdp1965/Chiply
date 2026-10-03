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
  test_edit.cpp
  test_netlist.cpp
  test_sim.cpp
  test_sim_cosim.cpp
  test_sim_board.cpp
)
target_link_libraries(chiply_tests PRIVATE chiply_sim Catch2::Catch2WithMain)
target_compile_definitions(chiply_tests PRIVATE
  CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")

include(Catch)
catch_discover_tests(chiply_tests)

# GUI tests (offscreen Qt platform)
if(CHIPLY_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(chiply_gui_tests test_gui_hover.cpp)
  set_target_properties(chiply_gui_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_hover COMMAND chiply_gui_tests)
  set_tests_properties(gui_hover PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_selection_tests test_gui_selection.cpp)
  set_target_properties(chiply_gui_selection_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_selection_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_selection_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_selection COMMAND chiply_gui_selection_tests)
  set_tests_properties(gui_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_wire_tests test_gui_wire_edit.cpp)
  set_target_properties(chiply_gui_wire_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_wire_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_wire_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_wire_edit COMMAND chiply_gui_wire_tests)
  set_tests_properties(gui_wire_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_editing_tests test_gui_editing.cpp)
  set_target_properties(chiply_gui_editing_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_editing_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_editing_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_editing COMMAND chiply_gui_editing_tests)
  set_tests_properties(gui_editing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_window_tests test_gui_window.cpp)
  set_target_properties(chiply_gui_window_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_window_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_window_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_window COMMAND chiply_gui_window_tests)
  set_tests_properties(gui_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_wiredraw_tests test_gui_wires.cpp)
  set_target_properties(chiply_gui_wiredraw_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_wiredraw_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_wiredraw_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_wire_draw COMMAND chiply_gui_wiredraw_tests)
  set_tests_properties(gui_wire_draw PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_clipboard_tests test_gui_clipboard.cpp)
  set_target_properties(chiply_gui_clipboard_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_clipboard_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_clipboard_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_clipboard COMMAND chiply_gui_clipboard_tests)
  set_tests_properties(gui_clipboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# CLI simulation script on the Tiny Tapeout template.
add_test(NAME cli_sim_template
  COMMAND chiply-cli sim ${PROJECT_SOURCE_DIR}/reference/tt_template_354858054593504257.diagram.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/tt_template.sim)
add_test(NAME cli_sim_template_board
  COMMAND chiply-cli sim ${PROJECT_SOURCE_DIR}/reference/tt_template_354858054593504257.diagram.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/tt_template_board.sim)

if(CHIPLY_BUILD_GUI)
  add_executable(chiply_gui_sim_tests test_gui_sim.cpp)
  set_target_properties(chiply_gui_sim_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(chiply_gui_sim_tests PRIVATE chiply_ui Qt6::Test)
  target_compile_definitions(chiply_gui_sim_tests PRIVATE
    CHIPLY_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/reference")
  add_test(NAME gui_sim COMMAND chiply_gui_sim_tests)
  set_tests_properties(gui_sim PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
