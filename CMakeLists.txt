
cmake_minimum_required(VERSION 3.21)

project(CanvasPDF VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)

add_executable(CanvasPDF
    src/main.cpp
)

target_link_libraries(CanvasPDF PRIVATE
    Qt6::Widgets
    PkgConfig::MUPDF
)

if(MSVC)
    target_compile_options(CanvasPDF PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(CanvasPDF PRIVATE
        $<$<CONFIG:Release>:-O2>
    )
endif()

include(GNUInstallDirs)

install(TARGETS CanvasPDF
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
