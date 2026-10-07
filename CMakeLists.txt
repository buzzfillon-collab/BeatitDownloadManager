cmake_minimum_required(VERSION 3.24)
project(BeatitDownloadManager VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(CURL REQUIRED)
find_package(LibtorrentRasterbar CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(BeatitDownloadManager WIN32
    src/main.cpp
    src/app/MainWindow.cpp src/app/MainWindow.h
    src/core/DownloadManager.cpp src/core/DownloadManager.h
    src/core/DownloadTask.cpp src/core/DownloadTask.h
    src/core/DownloadDatabase.cpp src/core/DownloadDatabase.h
    src/core/HttpDownloader.cpp src/core/HttpDownloader.h
    src/core/TorrentEngine.cpp src/core/TorrentEngine.h
    src/core/YtDlpManager.cpp src/core/YtDlpManager.h
    src/browser/BrowserBridge.cpp src/browser/BrowserBridge.h
    src/browser/BrowserInstaller.cpp src/browser/BrowserInstaller.h)

target_include_directories(BeatitDownloadManager PRIVATE src)

add_executable(BeatitBrowserHost WIN32 src/browser/NativeHost.cpp)
target_link_libraries(BeatitBrowserHost PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(BeatitDownloadManager PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(BeatitDownloadManager PRIVATE Qt6::Widgets Qt6::Network CURL::libcurl LibtorrentRasterbar::torrent-rasterbar unofficial::sqlite3::sqlite3)

if(MSVC)
    target_compile_options(BeatitDownloadManager PRIVATE /W4 /permissive-)
else()
    target_compile_options(BeatitDownloadManager PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS BeatitDownloadManager BeatitBrowserHost RUNTIME DESTINATION .)
