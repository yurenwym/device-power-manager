# 已验证的查询与解析

所有偏移从报文的第 0 字节（包含 Report ID）计算。本实现独立用 C++/Windows SDK 编写，不打包官方驱动或参考项目的二进制。

## HyperX Cloud III Wireless

- 本机：`03F0:0FCC`，usage `FF13:0001`，IN/OUT 均 62 字节。
- 电量请求 `66 89`，补零到 62 字节；回复首字节 `66`，命令 `89` 或响应 `0D`，字节 2/3 至少一个非零，字节 4 为百分比且不得大于 100。
- 实测响应前缀：`66 89 0F AF 57` → 87%；后续自然放电读到 86%。
- 充电请求 `66 8A`，回复命令 `8A`/`0C`，字节 2：0 使用中、1 充电、2 充满。本机验证 0 状态；充电切换尚未插拔验证。
- 协议参考 [HyperHeadset Cloud III](https://github.com/LennardKittner/HyperHeadset/blob/main/src/devices/cloud_iii_wireless.rs) 的 `66` 协议说明；参考项目所列 PID 不包含本机 `0FCC`，本机 PID 通过实际查询确认后单独加入。

## Ninjutso Sora V2

- 本机：`1915:AE1C`，usage `FFA0:0001`。
- 32 字节 Feature 请求 `05 15 00 00 01`，补零；等 90ms 后读取 Report 05。
- 验证报文头；字节 9 百分比、10 充电、11 满电、12 在线。只有在线标志为 1 且电量 0–100 时接受。
- 本机回复前缀 `05 15 00 00 01 00 00 00 00 56 00 00 01` → 86%、在线、未充电。
- 参考：[Sora Tray 查询实现](https://github.com/Fan4Metal/Sora_tray/blob/master/sora_tray.py)。
- 有线 PID AE11 为协议入口，未经本机验证，不计入已验证支持。

## 迈从 Z75S / iLovbee B98

- 两只接收器均 `258A:01BB`，usage `FF02:0002`，IN/OUT 为 20 字节。必须进一步读型号；不能只按 VID/PID 归类。
- 通用请求：`13 <cmd> 01`，字节 3–18 为零，字节 19 是前 19 字节之和的低 8 位。
- 标识查询命令 `05`，回复 `13 05 01 00 06 <六字节标识> ... <checksum>`。
  - Z75S：`0A 00 00 00 00 09`。与已安装迈从 `YProduct.xml` 的 `Productpassword` 一致。
  - B98：`0A 00 00 00 00 10`。与已安装 iLovbee `Dev/01E0/KB.ini` 的 `Psd=a,0,0,0,0,10` 一致。
- 电量查询命令 `4A`：`13 4A 01 00 ... 5E`。
- 回复 `13 4A 01 00 02 <percent> <state> ... <checksum>`，校验长度、命令、有效载荷长度和完整字节和。
  - Z75S：`13 4A 01 00 02 5A 00 ... BA` → 90%，后续读到 89%。
  - B98 唤醒后：`13 4A 01 00 02 64 01 ... C5` → 100%、满电。
- 状态 `0x10` 为充电、`0x01` 为满电、`0x00` 为使用中；其他未知位拒绝显示。充电位基于同协议公开实现，本机验证了 00/01，尚未测试充电线切换。
- 同协议参考：[MCHOSE HID bridge 协议解析](https://github.com/arutkowski00/mchose-hid-bridge/blob/main/src/protocol.c)。该项目目标 K99，本项目分别通过上述两款的真机返回值和本机官方型号表验证适配。
- B98 休眠时接收器存在但不回复；唤醒后两次电量读取一致。不用接收器的存在推断设备在线。

## Windows 传输

- 属性枚举以 0 访问权限打开接口；厂商查询仅匹配已验证的接口，避免访问系统独占的鼠标/键盘主集合。
- 报文使用 OVERLAPPED、事件、超时、CancelIoEx，先完成发送再读取，避免阻塞读与写并发。
- 输出报告优先使用 SET_OUTPUT_REPORT IOCTL，不支持时才退到异步 WriteFile。
- 超时取消后只额外等 100ms；如果驱动仍不完成，保留整个请求的句柄/缓冲区/OVERLAPPED，阻止同路径继续发请求。池上限 16，防止故障驱动导致无限等待或内存持续增长。
- Windows 枚举/驱动属性系统调用自身不在报文层超时控制范围内。设备休眠不能保证被查询唤醒，请实际操作设备后刷新。

## 参考方法

[2.4G-battery-viewer](https://github.com/yszyszysz233/2.4G-battery-viewer)：参考其 ctypes 调用 Windows HID 的思路、按设备验证协议的方法、超时及托盘退出经验。本项目运行时不加载该 Python 仓库。
