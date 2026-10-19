cmake_minimum_required(VERSION 3.16)
project(apokolips-shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets Svg OpenGL OpenGLWidgets)
find_package(qtermwidget6 REQUIRED)

add_executable(apokolips-shell main.cpp)

target_link_libraries(apokolips-shell PRIVATE
    pam
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    qtermwidget6)
