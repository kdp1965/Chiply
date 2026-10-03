# chiply_vl: optional Verilator backend (PLAN.md 6.6). Needs nothing at build
# time; Verilator and a C++ compiler are looked up when a design is run.
find_package(Threads REQUIRED)
add_library(chiply_vl STATIC
  VerilatorChip.cpp
)
target_include_directories(chiply_vl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(chiply_vl PUBLIC chiply_sim Threads::Threads ${CMAKE_DL_LIBS})
