# ax201-macos

基于 [laobamac/itlwm](https://github.com/laobamac/itlwm) 和
[OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm) 的 AX201 驱动开发项目。
保留上游历史，逐步完善原生 Wi-Fi、WPA3 和 AirDrop。
当前优先开发 AirDrop。

**目前是实验开发阶段，尚不支持 WPA3-only 和原生 AirDrop。**
接收到 AWDL 帧、编译通过或出现接口，都不能算 AirDrop 已经可用。

## 首批验证范围

Intel AX201、ROG STRIX B660-I GAMING WIFI、macOS Sequoia 15.8.1（24H32）、x86_64。
上游 Sequoia 目标面向 15.2+，但本项目没有验证这一范围内的所有系统版本。
Tahoe 和其他网卡的源码保留，暂不承诺兼容性。

| 功能 | 现状 |
| --- | --- |
| 原生 Wi-Fi 菜单、扫描 | 在开发机器上可用 |
| WPA2-Personal 2.4 GHz | 已连接，完成短时连通性和诊断恢复测试 |
| 5 GHz | 能扫描到网络，完整连接和稳定性测试待补 |
| WPA2/WPA3 混合模式、可选 PMF | 上游声明支持，本项目尚未独立验证 |
| WPA3-SAE-only、强制 PMF | 尚未支持 |
| AWDL 帧解析和时序 | 已有离线测试和独占监听记录，仅用于诊断 |
| AWDL IPv6 数据接收 | Revision 10 已在信道 149 实测；108 个 1280 字节包通过传输层校验，仅限诊断接收 |
| AWDL 固定数据发送 | Revision 11 的 64/1280/1500 字节测试包已得到固件完成通知；有效回包为零，尚未实现双向 IPv6 |
| Wi-Fi 连接时的 AWDL 接收 | 诊断退出和恢复已测试，同信道对端接收尚未证实 |
| 原生 AWDL 对象生命周期 | Revision 12 的 27 次初始化和释放通过；原生启动探针仍在实验阶段，数据队列尚未实现 |
| 原生 awdl0、双向 IPv6 互通、AirDrop 传文件 | 尚未实现 |
| 睡眠唤醒、漫游、持续吞吐 | 尚未达到发布验收标准 |

## 本地构建

在安装了 Python 3 和 Apple Command Line Tools 或 Xcode 的 Mac 上运行：

```sh
python3 scripts/bootstrap_sdk.py
python3 scripts/test.py
python3 scripts/build_sequoia.py --variant wifi
```

默认构建不包含实验 AWDL 用户接口。开发者可以用 `--variant awdl` 显式开启。
产物分别在 `build/wifi/` 和 `build/awdl/`，都叫 `AirportItlwm.kext`，不能同时加载。
构建脚本不会安装驱动或修改 EFI。Apple Silicon Mac 可以交叉编译，不能加载这个 x86_64 驱动。

详情见[构建说明](docs/BUILDING.md)、[验证记录](docs/VALIDATION.md)和[开发路线](docs/ROADMAP.md)。
CI 只做测试、编译和记录产物来源；二进制发布需另行完成硬件验收。

## 参与和许可

欢迎提交可复现的问题、测试结果和代码改进。报告请注明提交号、构建类型、网卡型号、
系统版本、网络认证类型和复现步骤，不要上传密码、完整 EFI 或未经脱敏的网络抓包。

项目保留上游 GPL 许可证及文件中的原作者声明，Intel 固件保留单独的再分发许可证。
致谢与来源见 [PROVENANCE.md](docs/PROVENANCE.md)。
