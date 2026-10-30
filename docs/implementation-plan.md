# WirelessBattery Implementation Plan

**Goal:** 实现可直接运行的 C++ + Win32 无线设备电量托盘工具。
**Architecture:** SetupAPI/HID 传输层、设备适配器、后台扫描、主线程界面。
**Tech Stack:** C++17 / MSVC / Win32 / GDI / HID / SetupAPI。
**Spec:** design.md，用户已确认技术与产品方向。

## Global Constraints
- Windows 10/11 x64；无第三方运行时。
- 未验证的数据不显示为真实电量；只发送已确认用途的查询命令。
- UI 不进行设备 I/O；同一时刻至多一个刷新任务；所有报文异步 I/O 必须有界等待并保持请求生命周期。

## Task 1: 协议验证和传输
- [x] 从官方软件及公开协议确认键盘读命令，分别标识物理接收器。
- [x] 创建 src/hid.hpp、src/hid.cpp：`std::vector<Interface> enumerate()`；`Channel::exchange()` 有超时、取消和句柄释放。
- [x] 创建 src/protocols.hpp 与 tests/protocol_tests.cpp，先测试异常报文，再实现纯解析器。
- [x] 创建 src/adapters.hpp、src/adapters.cpp：`std::vector<Reading> scan()`；精确匹配与状态输出。
- [x] 用 `BatteryProbe.exe --once` 核对本机真实返回值，失败时明确原因。

## Task 2: 常驻程序
- [x] 创建 src/app.cpp：单实例互斥锁、隐藏控制窗口、SetTimer 后台刷新、PostMessage 数据交付。
- [x] 实现原生中文面板、状态文字与进度条、刷新间隔和提醒阈值设置、托盘菜单。
- [x] 增加 Startup 文件夹快捷方式开关，处理 Explorer 重启与退出回收。
- [x] 编译 Release x64，验证重复启动不产生第二实例，面板关闭保留托盘，退出不残留进程。

## Task 3: 交付与测量
- [x] 创建 build.cmd、CMakeLists.txt、README.md、协议证据与扩展说明。
- [x] 测量空闲 CPU、工作集、私有内存、文件大小；实际结果写入验证报告。
- [x] 打包 EXE、源代码及说明，用户可直接运行且不依赖 Python。
