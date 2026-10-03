find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# Everything except main(): shared by the app and the GUI tests.
qt_add_library(chiply_ui STATIC
  MainWindow.cpp MainWindow.h
  EditorSession.cpp EditorSession.h
  SchematicView.cpp SchematicView.h
  Theme.cpp Theme.h
  SymbolPainter.cpp SymbolPainter.h
  SchematicItems.cpp SchematicItems.h
  PartPalette.cpp PartPalette.h
)
target_include_directories(chiply_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chiply_ui PUBLIC chiply_core Qt6::Widgets)

qt_add_executable(chiply MACOSX_BUNDLE main.cpp)
target_link_libraries(chiply PRIVATE chiply_ui)
target_compile_definitions(chiply PRIVATE CHIPLY_VERSION="${PROJECT_VERSION}")
set_target_properties(chiply PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME Chiply
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.kdp1965.chiply
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
