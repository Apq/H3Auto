# lzma/ — LZMA SDK（7-Zip 官方，public domain）

来源：https://www.7-zip.org/sdk.html （Igor Pavlov，公有领域）。
只取日志打包所需的最小子集：LZMA 编码器 + 匹配器 + CRC/CPU 探测。

## 与上游的差异（升级 SDK 时需重放）

- `7zTypes.h`：`#endif`（include guard 结束）原写在 `EXTERN_C_END` 之后。
  include guard 命中时（同编译单元已先包含过）extern "C" 块不会闭合，
  后续 C++ 代码被卷进 C 链接。已把 `#endif` 挪到 `EXTERN_C_END` 之前。

## 集成要点

- vcxproj 预定义 `Z7_ST`：关闭多线程匹配器（否则需再链 LzFindMt.c/Threads.c）。
- 调用 `LzmaEncode` 的 C++ 代码 include 顺序：`LzmaEnc.h` 在 `<windows.h>` 之前
  （7zTypes.h 注释掉了 windows.h 且 extern "C" 包到文件尾）。
