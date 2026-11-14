# CAN.Hardware(C++ 版)— 纯 C++17 多厂商 CAN / CAN FD 硬件抽象库

> ## ⚠️ 关于本项目
>
> **本项目源自 [jsonwu-car/CAN.Hardware](https://github.com/jsonwu-car/CAN.Hardware) 的 .NET 版本,
> 是它的完整纯 C++ 重写版。**
>
> - 原 .NET 工程(12 家厂商、23 个 Wrapper)完整保留在 **`original-dotnet` 分支**
>   (提交 `4bdbb83`,含原 README、`CAN.Hardware/`、`CAN.Hardware.Demo/`)。
> - 当前 `main` 已将其**全部替换为纯标准 C++17 实现**,以 .NET 工程为唯一事实来源逐文件移植:
>   注册键 25/25 一一对应、驱动 DLL 路径逐条一致、打开序列/错误文案/DLC 与时间戳换算/过滤器语义
>   均与原 .NET 版相同。
> - `HwDependency/` 下的厂商原生驱动 DLL 沿用原 .NET 工程,目录结构保持一致。

---

## 一、这个工程解决什么问题

汽车电子 / 工业控制中,CAN 总线是设备通信的主流方式,但各家硬件厂商(Kvaser、PEAK、周立功、Vector 等)
的驱动 API、P/Invoke 方式、枚举定义、配置参数、报文标志位各不相同。若每个项目直接调用厂商驱动,
就会出现:换一家设备所有收发代码重写、配置方式五花八门、标志位含义互不通用。

本库把「厂商驱动的差异性」与「上层业务逻辑」彻底解耦:上层只面对统一接口 `IHardware`,
具体硬件由工厂按 `厂商 → 设备类型 → 协议` 三层枚举自动匹配。**纯标准 C++17,不依赖 Qt / Boost
等任何第三方框架**(仅 STL + 平台动态库 API)。

---

## 二、支持的厂商(12 家 / 23 个 Wrapper / 25 个注册键)

| 厂商 | 驱动 DLL | 驱动风格 | 注册键(厂商/设备类型/协议) | 接收方式 |
|---|---|---|---|---|
| ZLG 周立功 | zlgcan.dll | 句柄式 | Zlg/USBCAN/CAN、Zlg/USBCANFD200U/{CANFD,CAN} | 轮询 |
| PEAK | PCANBasic.dll | 句柄式 | Peak/Generic/{CAN,CANFD} | 轮询 |
| Kvaser | canlib32.dll | 句柄式 | Kvaser/Generic/{CAN,CANFD} | 轮询(TXBUFOFL 重试≤5) |
| 创芯 ChuangXin | ControlCAN.dll + ControlCANFD.dll | 批量缓冲 | ChuangXin/USBCAN/CAN、ChuangXin/USBCANFD/{CANFD,CAN} | 轮询 |
| 同星 Tosun | libTSCAN.dll | **事件驱动** | Tosun/Generic/{CAN,CANFD} | 驱动回调(按通道路由) |
| 图莫斯 Toomoss | USB2XXX.dll | 批量缓冲 | Toomoss/Generic/{CAN,CANFD} | 轮询 |
| Vector | vxlapi64.dll | 句柄式 | Vector/Generic/{CAN,CANFD} | 轮询(时序读回校验) |
| ESD | ntcan64.dll(系统路径) | 句柄式 | Esd/Generic/{CAN,CANFD} | **阻塞读 200ms 不持锁** |
| NI-XNET | nixnet.dll(系统路径) | 会话/流式 | Ni/Generic/{CAN,CANFD} | 轮询(帧流逐帧解析) |
| ETAS | dll-ocdProxy + dll-csiBind | **回调驱动** | Etas/Generic/{CAN,CANFD} | 驱动回调(userData=实例) |
| Intrepid | icsneo40.dll(系统路径) | 句柄式 | Intrepid/Generic/{CAN,CANFD} | 驱动级等待(200ms) |
| EbEl FlexCard | fcbase.dll(系统路径) | 缓冲发送+链表接收 | EbEl/Generic/CAN(**仅经典 CAN**) | 轮询(先遍历后归还) |

驱动来源:
- **随工程携带**(`HwDependency/`,运行时按相对路径或 exe 同级目录解析):
  ZLG、PEAK、Kvaser、创芯、同星、图莫斯、Vector
- **系统安装**(裸 DLL 名按系统搜索路径解析):
  ESD、NI-XNET、Intrepid、EbEl;ETAS 额外查注册表
  `HKLM\SOFTWARE\ETAS\BOA\2\PATH` + Program Files 默认路径回退

---

## 三、.NET → C++ 关键映射

| .NET 版设施 | 作用 | C++ 版实现 |
|---|---|---|
| P/Invoke `DllImport` | 调厂商 C DLL | `DynamicLibrary`(`LoadLibrary`/`GetProcAddress`)运行时动态绑定,免 .lib,缺 DLL 时也能编译 |
| `[HardwareDefAttributes]` + 反射扫描 | 三层枚举自动匹配 | **静态注册表**:`Factory::Register()` + `RegisterBuiltinHardware()` |
| `IHardware` 接口(5 成员) | 统一抽象 | `canhw::IHardware`(虚函数,一一对应) |
| `EventHandler<FrameBase>` | 收发回调 | `std::function<void(const FrameBase&)> eventHandler` |
| `INotifyPropertyChanged` | WPF 属性绑定 | 省略(纯逻辑层不需要) |
| `IXmlSerializer` | 配置 XML 持久化 | 暂未移植(配置为普通结构体) |
| `Task.Run` + `CancellationTokenSource` | 后台读线程 | `Sync.h` 的 `Thread(Start/Join)` + `atomic<bool>` |
| `std::thread` / `std::mutex` | 标准线程原语 | 经 `Sync.h` 兼容层,本机 MinGW 下退化为 Win32(见第六节) |
| `Marshal.PtrToStructure` | 结构体还原 | 原生结构体直接访问(layout 经 `static_assert` 校验) |

方法命名与 .NET 版保持一致(`Open/Close/Send/Config/eventHandler`),数据字段用 camelCase。

---

## 四、目录结构

```
CAN.Hardware/                     仓库根(C++ 项目主体)
├── CMakeLists.txt
├── README.md                     本文件
├── include/canhw/                对外头文件(使用方只需 include <canhw/Canhw.h>)
│   ├── Canhw.h                   伞头文件
│   ├── Enums.h                   厂商/协议/设备类型枚举(12 家)
│   ├── Frame.h                   FrameBase / CANFrame / BusFrameFlags / CanFrameFlags
│   ├── IHardware.h               统一硬件接口
│   ├── Factory.h                 工厂(静态注册表,替代反射)
│   ├── CanFrameHelper.h          SocketCAN 风格 ID 解析
│   ├── DataLenConverter.h        DLC ⇄ 长度换算与校验
│   ├── ChannelInfo.h             通道清单条目(供「刷新设备」展示)
│   ├── Discovery.h               设备/通道枚举 API(PEAK 已实现)
│   ├── Sync.h                    线程兼容层(无线程模型的 MinGW→Win32,其余→std)
│   └── XxxConfig.h ×12           各厂商配置结构体 + 厂商专属枚举
├── src/
│   ├── Frame.cpp / Factory.cpp / RegisterBuiltin.cpp / DynamicLibrary.*
│   ├── zlg/       ZlgApi + FlagParser + 2 Wrapper + Register
│   ├── peak/      PeakApi + PeakDiscovery + FlagParser + 2 Wrapper + Register
│   ├── kvaser/    KvaserApi + FlagParser + 2 Wrapper + Register
│   ├── chuangxin/ ChuangXinCanApi + ChuangXinCanFdApi + 2 Wrapper + Register
│   ├── tosun/     TosunApi(事件分发)+ FlagParser + 2 Wrapper + Register
│   ├── toomoss/   ToomossApi + FlagParser + 2 Wrapper + Register
│   ├── vxl/       VectorApi + FlagParser + 2 Wrapper + Register(Vector→vxl 防 std::vector 冲突)
│   ├── esd/       EsdApi + FlagParser + 2 Wrapper + Register
│   ├── ni/        NiApi(帧流打包/解析)+ FlagParser + 2 Wrapper + Register
│   ├── etas/      EtasBoaApi(注册表定位+CSI 枚举)+ FlagParser + 2 Wrapper + Register
│   ├── intrepid/  IntrepidApi + FlagParser + 2 Wrapper + Register
│   └── ebel/      EbElApi + FlagParser + 1 Wrapper(CAN only)+ Register
├── demo/main.cpp                 演示:列清单 / 通道枚举 / 单通道与双通道收发
└── HwDependency/                 厂商原生驱动 DLL(沿用原 .NET 工程)
```

---

## 五、构建与运行

### 方式 A:CMake(有 cmake 的环境)

```bash
cd CAN.Hardware
cmake -B build -A x64        # MSVC;MinGW 用 -G "MinGW Makefiles"
cmake --build build --config Release
```

### 方式 B:g++ 直接编译(无 cmake 的 MinGW 环境,已实测全绿)

```bash
cd CAN.Hardware
mkdir -p build_gcc
for f in $(find src demo -name '*.cpp'); do
  g++ -std=c++17 -Iinclude -Isrc -c "$f" -o "build_gcc/$(echo $f | tr '/' '_').o" || echo "FAIL: $f"
done
g++ build_gcc/*.o -o build_gcc/canhw_demo.exe -ladvapi32
./build_gcc/canhw_demo.exe
```

### 运行(在仓库根目录,`HwDependency/` 就在旁边)

```bash
./build_gcc/canhw_demo.exe              # 列出全部 12 家硬件清单(无需硬件)
./build_gcc/canhw_demo.exe peaklist     # 枚举 PEAK 通道(名称/可用性,零副作用)
./build_gcc/canhw_demo.exe peak         # PEAK 经典 CAN 收发(需 PCAN-USB)
./build_gcc/canhw_demo.exe peakfd       # PEAK CAN FD(需 FD 设备)
./build_gcc/canhw_demo.exe peak2ch      # PEAK 双通道互连(持续发送 10s)
./build_gcc/canhw_demo.exe zlg          # ZLG 经典 CAN 收发
./build_gcc/canhw_demo.exe zlgfd        # ZLG CAN FD
```

> 收发 demo 需要插对应设备并安装厂商驱动,否则 `Open` 抛出带指导信息的异常
> (如"请先安装 ESD CAN 驱动"/"未发现任何 ETAS CAN 硬件节点")。
> 无参数运行只打印已注册硬件清单,不需要任何硬件。

---

## 六、工具链说明(重要)

本机 MinGW(gcc8,win32 线程模型)的 libstdc++ **没有 `std::mutex/std::thread`**
——QC2015P_Cpp_Plus 参考工程也因此改用 Win32 API(见其 `main_final.cpp` 注释)。
本工程通过 [include/canhw/Sync.h](include/canhw/Sync.h) 兼容:

- 无线程模型的 MinGW → `CRITICAL_SECTION` + `CreateThread`
- MSVC / posix 线程 MinGW → 标准库

业务代码统一使用 `Mutex / LockGuard / Thread(Start/Join) / SleepMs`,两种工具链零改动。

---

## 七、使用示例(与 .NET 版三步走一致)

```cpp
#include <canhw/Canhw.h>
using namespace canhw;

int main() {
    RegisterBuiltinHardware();   // 静态库必须先注册(对应 .NET 反射)

    // ① 工厂按三层枚举取实例（12 家用法完全一致）
    auto hw = Factory::GetInstance(HardwareManufacturer::Kvaser,
                                   KvaserDeviceType::Generic, CommProtocol::CAN);

    // ② 配参数（static_cast 到具体配置类型）
    auto& cfg = static_cast<KvaserCANConfig&>(hw->Config());
    cfg.canDeviceChannel = 0;
    cfg.canBitrate = KvaserCanBitrate::canBITRATE_500K;

    // ③ 订阅报文 → 打开 → 发送 → 关闭
    hw->eventHandler = [](const FrameBase& f) {
        if (auto* cf = dynamic_cast<const CANFrame*>(&f))
            if (cf->flags) printf("%s\n", cf->flags->ToString().c_str());
    };
    hw->Open();
    CANFrame frame;
    frame.id = 0x123; frame.dlc = 8;
    frame.data = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    hw->Send(frame);
    hw->Close();
}
```

通道枚举(GUI「刷新设备」):

```cpp
#include <canhw/Discovery.h>
for (const auto& ch : ListPeakChannels())
    printf("0x%X %s %s\n", ch.channel, ch.name.c_str(),
           ch.available ? "available" : "occupied");
```

---

## 八、新增一家厂商(对照 .NET 版 8 步)

1. `include/canhw/Enums.h`:加设备类型枚举;
2. `src/Factory.cpp` 的 `ValidateEquipmentType`:登记该厂商枚举(强校验);
3. `src/<vendor>/XxxApi.*`:写 DLL 动态绑定(仿 `ZlgApi`,含原生结构体 + `static_assert` 布局校验);
4. 驱动 DLL 放入 `HwDependency/...` 或按系统路径解析,路径写进 `DefaultDllPath()`;
5. `src/<vendor>/XxxFlagParser.h`:原生 flag → 统一 `CanFrameFlags`;
6. `include/canhw/XxxConfig.h`:配置结构体(继承 `BusConfigBase`);
7. `src/<vendor>/XxxWrapper.*`:实现 `IHardware`(锁 + 读线程/回调 + 发送强校验);
8. `XxxRegister.cpp` 注册 + 在 `BuiltinRegistrations.h`、`RegisterBuiltin.cpp`、
   `CMakeLists.txt`、`Canhw.h` 中挂接。

**上层调用方式零改动**——`Factory::GetInstance(...)` 自动找到新注册的封装。

---

## 九、从 QC2015P_Cpp_Plus 借鉴的实践

| 借鉴点 | 参考工程出处 | 本工程落地 |
|---|---|---|
| **通道枚举「刷新设备」** | `ICanDevice::listChannels` + `CanChannelInfo` | `include/canhw/ChannelInfo.h`、`Discovery.h`;PEAK 已实现 `ListPeakChannels()`(用 `PCAN_CHANNEL_CONDITION` 探测,不打开通道、零副作用),demo 命令 `peaklist`。其他厂商按同一约定逐步补充 |
| **ZLG 驱动不稳定期容错** | `can_zlg.cpp`:InitCAN 失败 500ms 重试、深度恢复 | ZLG 两个 Wrapper 的 Open 增加 500ms 重试,失败提示"设备可能被占用或需重新插拔 USB"(共享设备句柄模型下不做整机 Close→Open 深度恢复,避免影响同机其他通道) |
| **接收先查数量** | `can_zlg.cpp`:`VCI_GetReceiveNum` | `ZlgApi::GetReceiveNum`(可选导出,旧驱动缺失时自动退化);ZLG 读循环先查队列数量,空队列不再发起无效接收调用 |
| **线程原语受限的应对** | `main_final.cpp`/`can_tosun.cpp` 注释 | `include/canhw/Sync.h` 兼容层 |
| SPSC 无锁队列 + 专用 I/O 线程 | `can_device.h` 的 `SpscRing` | **未采用**:本库以 .NET 版同步 `Send` 语义与逐 Wrapper 读线程为准;如后续需要更高实时性,可在 `IHardware` 之上另加队列层,不改动现有接口 |

---

## 十、真机验证记录

| 场景 | 结果 |
|---|---|
| PCAN-USB 单通道 500K 收发 | 打开/发送/回显/关闭全链路通过,退出码 0 |
| PCAN-USB 双通道互连(USBBUS1↔USBBUS2) | 双向收发:各发 50 帧(10 秒、200ms/帧、首字节序号),对端 50/50 全收、无丢帧、数据逐字节一致 |
| PEAK 通道枚举 | `peaklist` 实测可用(零副作用) |
| 其余 10 家 | 编译/链接通过;`Open` 在无设备时的错误路径已验证;待真实硬件补充记录 |

---

## 十一、未移植 / 已知差异

1. **配置持久化(IXmlSerializer/XML)与 WPF 属性通知未移植**(纯逻辑层无 GUI 依赖);
2. ZLG 时间戳按 .NET 参考实现以 **0.1ms → µs(×100)** 换算;Toomoss 经典 100µs×100、
   FD 10µs×10;NI 100ns÷10;Intrepid 双 32 位 tick 浮点换算;EbEl 单位未公开透传——均与 .NET 一致;
3. **同星 Tosun 的事件回调按通道号路由**(驱动回调不带设备上下文):同设备同通道的后注册者
   覆盖先注册者,与 .NET 中第二次 `tscan_register_event_can` 的行为一致;
4. **ETAS 回调以 `userData=this` 定位实例**,实例销毁前先 `Close` 使在途回调失效;
5. `Close()` 统一用 `Thread::Join()` 等待读线程退出(比 .NET 的 `Wait(1000)` 超时更严格,
   各家读循环均在 ≤200ms 内可退出);
6. **线程原语经 `Sync.h` 兼容层**:本机 MinGW 下底层是 Win32 临界区/`CreateThread`,
   MSVC / posix MinGW 下是标准库;
7. 仅 Windows 完成设计验证(`DynamicLibrary` 预留 POSIX `dlopen` 分支;
   ETAS 注册表定位与 `Sync.h` 的 Win32 分支为 Windows 专属,其他裸名加载可跨平台)。
