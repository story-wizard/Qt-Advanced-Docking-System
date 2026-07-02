cmake_minimum_required(VERSION 3.16)

# [Wizard NLE fork] Test suite for fork-specific changes. Gated behind
# BUILD_TESTS in the root CMakeLists.txt so downstream consumers (FetchContent,
# distributors) don't pick it up by default.

# QT_VERSION_MAJOR isn't propagated from src/CMakeLists.txt scope; redo the
# version detection here so the test target can find Qt::Test independently.
if(DEFINED QT_VERSION_MAJOR AND QT_VERSION_MAJOR)
    find_package(QT NAMES Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
else()
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
endif()
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets Test REQUIRED)

set(library_name "qtadvanceddocking-qt${QT_VERSION_MAJOR}")

function(add_ads_qt_test target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_link_libraries(${target} PRIVATE
        ${library_name}
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )

    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_ads_qt_test(QuadrantHitTestTest QuadrantHitTestTest.cpp)
add_ads_qt_test(OverlayGateTest OverlayGateTest.cpp)
