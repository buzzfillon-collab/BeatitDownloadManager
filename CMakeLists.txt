cmake_minimum_required(VERSION 3.24)
project(BeatitDownloadManager VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(CURL REQUIRED)
find_package(LibtorrentRasterbar CONFIG REQUIRED)

add_executable(BeatitDownloadManager WIN32
    src/main.cpp
    src/app/MainWindow.cpp src/app/MainWindow.h
    src/core/DownloadManager.cpp src/core/DownloadManager.h
    src/core/DownloadTask.cpp src/core/DownloadTask.h
    src/core/HttpDownloader.cpp src/core/HttpDownloader.h
    src/core/TorrentEngine.cpp src/core/TorrentEngine.h)

target_include_directories(BeatitDownloadManager PRIVATE src)
target_link_libraries(BeatitDownloadManager PRIVATE Qt6::Widgets CURL::libcurl LibtorrentRasterbar::torrent-rasterbar)

if(MSVC)
    target_compile_options(BeatitDownloadManager PRIVATE /W4 /permissive-)
else()
    target_compile_options(BeatitDownloadManager PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS BeatitDownloadManager RUNTIME DESTINATION .)