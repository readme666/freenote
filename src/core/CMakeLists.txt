cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

file(GLOB_RECURSE xournalpp_sources *.cpp *.h)

# GLOB does not make an existing build directory notice newly added files.
# Keep new standalone implementation units explicit so an incremental build
# always re-runs CMake and includes them.
list(APPEND xournalpp_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/control/tools/OneEuroFilter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/control/tools/OneEuroFilter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/control/tools/InkStrokeModelerAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/control/tools/InkStrokeModelerAdapter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/view/overlays/PredictiveStrokeToolView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/view/overlays/PredictiveStrokeToolView.h"
)
list(REMOVE_DUPLICATES xournalpp_sources)

if(NOT ENABLE_AUDIO)
    list(FILTER xournalpp_sources EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/src/core/audio/.*" )
    list(FILTER xournalpp_sources EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/src/core/control/AudioController.cpp" )
endif ()
set(xournalpp_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(xournalpp-core STATIC ${xournalpp_sources})
target_compile_features(xournalpp-core PUBLIC ${PROJECT_CXX_FEATURES})
target_compile_features(xournalpp-core PUBLIC cxx_std_20)
target_link_libraries(xournalpp-core
        PRIVATE xoj::util ${xournalpp_LDFLAGS}
        PUBLIC xoj::defaults xoj::external_modules
        )
target_include_directories(xournalpp-core PUBLIC ${xournalpp_include_dirs})
if(ENABLE_INK_STROKE_MODELER)
    target_link_libraries(xournalpp-core PRIVATE InkStrokeModeler::stroke_modeler)
    target_include_directories(xournalpp-core SYSTEM PRIVATE "${INK_STROKE_MODELER_SOURCE_DIR}")
    target_compile_definitions(xournalpp-core PUBLIC XOURNALPP_ENABLE_INK_STROKE_MODELER=1)
endif()
add_library(xoj::core ALIAS xournalpp-core)
