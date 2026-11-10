# tests/

主机端单元测试（对照 [StackChan firmware/tests](https://github.com/m5stack/StackChan/tree/main/firmware/tests)）：
独立于 ESP-IDF 构建系统的 CMake 工程，直接编译 `main/` 中的**纯逻辑源码**
（不依赖 ESP-IDF、LVGL 或硬件）配合测试文件运行。

## 运行

```bash
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

## 约定

- 一个被测模块对应一个 `test_<module>.c` + `add_executable`/`add_test` 条目。
- 被测源码必须可在主机编译：只用标准 C（stdlib/stdint/stdatomic 等），
  不 include ESP-IDF/LVGL 头——需要隔离的硬件/外部依赖由被测模块的分层
  设计保证（参考 `main/apps/apps.c` 的做法），必要时在测试内打桩。
- 依赖真实硬件的行为（驱动、屏幕、触摸）不在这里测；上机用 `tools/`
  脚本 + USB console 命令（SNAP/REBOOT）验证。
- 断言风格：简单 `expect()` + 非零退出码，失败信息带标签。
