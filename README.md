# 设备电源管理 · C++ / Win32

轻量的 Windows 10/11 x64 电量查看器。原生托盘、中文面板，不依赖 Python、Qt、Electron 或额外安装的 VC 运行库。

## 启动

仓库包含源码，首次使用请先按下方“编译与测试”生成程序。

双击 `bin\设备电源管理.exe`。

- 左键点击托盘：打开电量面板。
- 右键点击托盘：打开面板、立即刷新或退出。
- 托盘使用固定电源图标；悬停查看各设备电量，点击打开面板。
- 关闭/最小化面板：销毁面板窗口，托盘继续运行。
- 退出：通过托盘菜单退出。重复启动会打开已有实例的面板。
- 首次默认每 60 秒读取一次；可选择 30 秒至 30 分钟。
- 低电量阈值可选择 10%、15%、20%、30%。同一设备两小时内不重复提醒；电量恢复至阈值以上 5 个百分点会解除提醒抑制。
- “开机启动”由用户主动勾选，使用用户启动文件夹快捷方式。可在任务管理器中禁用。

查询只针对电量/状态，不修改键位、灯光、固件、休眠时间或设备配对。

## 本机验证的设备

| 设备 | 2.4G 接收器 | 识别/读取方式 |
|---|---|---|
| HyperX 飓风 3 Wireless | `03F0:0FCC` | usage page `FF13`，`66 89` 电量、`66 8A` 充电状态 |
| Ninjutso Sora V2 | `1915:AE1C` | usage page `FFA0`，Feature Report `05` / 命令 `15` |
| 迈从 Z75S | `258A:01BB` | `13 05` 标识 `0A0000000009`，`13 4A` 电量 |
| iLovbee B98 | `258A:01BB` | `13 05` 标识 `0A0000000010`，`13 4A` 电量 |

两把键盘按设备返回的标识区分，再按 Windows Container ID 绑定接收器；不会因为相同 VID/PID 混为一台。

休眠、关机或无线断开时，插着接收器不代表设备在线。没有有效回复时显示“休眠或离线”，不把上次电量冒充当前电量。首次启动就休眠的键盘可能显示“未识别的键盘接收器”；按键唤醒后刷新即可识别。已识别接收器会记住型号。

本版本主要验证 2.4G 模式。Sora 有线 PID `AE11` 有匹配入口但尚未真机验证；其他三款有线模式及蓝牙模式未适配。其他固件/接收器版本需重新核对协议。

## 配置与隐私

- 配置：`%LOCALAPPDATA%\WirelessBattery\settings.ini`。
- 保存刷新间隔、通知阈值、通知开关和接收器 Container ID 到型号的映射，不保存序列号、原始报文或电量历史。
- 软件不联网、不遥测、不后台写日志。诊断工具仅在控制台输出。
- 启动快捷方式：`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\设备电源管理.lnk`。
- 保留 `bin\设备电源管理.exe` 的路径；移动后可在新位置启动，重新勾选开机启动。
- 删除程序前可取消开机启动，再从托盘退出。配置目录可以按需手动删除。

## 诊断

在终端运行：

```powershell
.\bin\BatteryProbe.exe --once   # 四款设备当前电量
.\bin\BatteryProbe.exe --debug  # 电量和查询响应，不包含序列号
.\bin\BatteryProbe.exe --list   # HID 接口、VID/PID、usage、报文长度
.\bin\设备电源管理.exe --tray    # 仅托盘启动
.\bin\设备电源管理.exe --refresh # 请求已有实例刷新
.\bin\设备电源管理.exe --exit    # 正常退出已有实例
```

诊断工具成功全部读取时返回 0；存在未连接或读取失败的设备时返回 2。输出重定向为 UTF-8。

## 编译与测试

安装 Visual Studio 2022 C++ Build Tools，包含 Windows SDK 和 x64 MSVC。

```cmd
build.cmd
```

脚本会定位 Visual Studio，先运行报文解析和异步 I/O 取消测试，再生成 `bin` 下的程序。全部采用静态 C/C++ 运行库，发布程序只加载 Windows 系统 DLL。

也可以使用 CMake（3.21+）：

```powershell
cmake -S . -B cmake-build -G "Visual Studio 17 2022" -A x64
cmake --build cmake-build --config Release
ctest --test-dir cmake-build -C Release --output-on-failure
```

## 扩展设备

- `src/hid.*`：枚举、物理设备标识和异步传输。
- `src/protocols.hpp`：不依赖 Windows 的报文构造及解析函数。
- `src/adapters.*`：设备匹配、独立读取逻辑和快照。
- `src/settings.*`：INI 和自启快捷方式。
- `src/app.cpp`：消息循环、托盘、后台线程、原生面板。

新增设备时，在 `adapters.cpp` 增加准确的 VID/PID、usage、报文长度匹配和设备查询函数，在 `protocols.hpp` 增加纯解析器及对应测试。返回 `Reading` 后，界面和托盘自动使用新数据，不需要为新型号添加 UI 分支。重复型号按接收器分别显示。

还包含标准 HID Feature Report 的 Battery Strength (`usage 0006:0020`) 读取入口；这不意味着所有无线设备都支持。未知私有协议需要独立适配，不盲发厂商命令。

协议证据及边界见 `docs/protocols.md`；实测资源与验证结果见 `docs/verification.md`。
