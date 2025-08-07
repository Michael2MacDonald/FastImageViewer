cmake_minimum_required(VERSION 3.10)
project(FastImageViewer)

set(CMAKE_CXX_STANDARD 17)

# Find Qt (prefer Qt6, fallback to Qt5)
find_package(Qt6 COMPONENTS Widgets REQUIRED)
set(QT_LIBS Qt6::Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_LIBS Qt5::Widgets)
endif()

# Find libraw
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)
include_directories(${LIBRAW_INCLUDE_DIRS})
link_directories(${LIBRAW_LIBRARY_DIRS})

add_executable(FastImageViewer src/main.cpp)

target_link_libraries(FastImageViewer ${QT_LIBS} ${LIBRAW_LIBRARIES})

# For modular/extensible design, add more source files as needed
# Example: add_subdirectory(src/modules)
