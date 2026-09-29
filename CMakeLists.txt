cmake_minimum_required(VERSION 3.19)
project(Snake LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia)

qt_standard_project_setup()

# Qt 6.11 的 qt_standard_project_setup() 不再自动开启 AUTORCC，
# 手写 .qrc 资源文件必须在这里显式打开，否则列进源文件列表的
# resources.qrc 会被静默忽略（编译能过，但资源不会进 exe）。
set(CMAKE_AUTORCC ON)

qt_add_executable(Snake
    WIN32 MACOSX_BUNDLE
    main.cpp
    snakewindow.cpp
    snakewindow.h
    resources.qrc
)

target_link_libraries(Snake
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
)

install(TARGETS Snake
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Snake
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
