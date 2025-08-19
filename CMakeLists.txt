cmake_minimum_required(VERSION 3.10)
project(FastImageViewer)

set(CMAKE_CXX_STANDARD 17)

# Find Qt (prefer Qt6, fallback to Qt5)

# Find Qt (prefer Qt6, fallback to Qt5)
find_package(Qt6 COMPONENTS Core Gui Widgets Concurrent REQUIRED)
set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Gui Widgets Concurrent REQUIRED)
    set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Concurrent)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)
include_directories(${LIBRAW_INCLUDE_DIRS})
link_directories(${LIBRAW_LIBRARY_DIRS})

# Find fmt
find_package(fmt REQUIRED)

add_executable(FastImageViewer src/main.cpp)


target_link_libraries(FastImageViewer PRIVATE ${QT_LIBS} ${LIBRAW_LIBRARIES} fmt::fmt)

# For modular/extensible design, add more source files as needed
# Example: add_subdirectory(src/modules)
