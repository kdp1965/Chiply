# chiply_sim: the built-in event-driven simulator (PLAN.md 6). Plain C++.
add_library(chiply_sim STATIC
  Simulator.cpp
  Trace.cpp
  TruthTable.cpp
  Bench.cpp
)
target_include_directories(chiply_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(chiply_sim PUBLIC chiply_core)
