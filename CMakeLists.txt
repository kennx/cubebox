# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(EXCLUDE_COMPONENTS esp_wifi esp_coex wpa_supplicant)

# 固件版本：进入 PING 的 #HELLO 响应（网页配置器显示）；须在 project() 前设置
set(PROJECT_VER 0.3.0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cubebox)
