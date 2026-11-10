# CubeBox 主机端单元测试工程（对照 StackChan firmware/tests）。
# 独立于 ESP-IDF 构建系统：直接编译 main/ 中的纯逻辑源码 + 测试文件。
# 运行方式见 tests/README.md。
cmake_minimum_required(VERSION 3.16)

project(cubebox_tests LANGUAGES C)

enable_testing()

add_executable(test_apps
    test_apps.c
    ../main/apps/apps.c
)

target_include_directories(test_apps PRIVATE ../main)
target_compile_options(test_apps PRIVATE -Wall -Werror -fsanitize=address,undefined)
target_link_options(test_apps PRIVATE -fsanitize=address,undefined)

add_test(NAME test_apps COMMAND test_apps)

add_executable(test_dotmatrix
    test_dotmatrix.c
    ../main/apps/common/dotmatrix/dotmatrix.c
)

target_include_directories(test_dotmatrix PRIVATE ../main)
target_compile_options(test_dotmatrix PRIVATE -Wall -Werror -fsanitize=address,undefined)
target_link_options(test_dotmatrix PRIVATE -fsanitize=address,undefined)

add_test(NAME test_dotmatrix COMMAND test_dotmatrix)
