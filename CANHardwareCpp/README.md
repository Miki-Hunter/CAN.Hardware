# CANHardwareCpp — 纯 C++ 版多厂商 CAN / CAN FD 硬件抽象库

.NET 版 [CAN.Hardware](../README.md) 的 **纯标准 C++17 移植**,
**不依赖 Qt / Boost 等任何第三方框架**(仅 STL + 平台动态库 API)。
**已移植全部 12 家厂商**(23 个 Wrapper,与 .NET 版一致;EbEl 仅经典 CAN)。

> 本目录与 .NET 工程同仓库共存:移植以 **.NET 工程为唯一事实来源**
> (注册键 25/25、驱动 DLL 路径、打开序列、错误文案均与其逐条对齐)。

---

## 一、.NET → C++ 关键映射

| .NET 版设施 | 作用 | C++ 版实现 |
|---|---|---|
| P/Invoke `DllImport` | 调厂商 C DLL | `DynamicLibrary`(`LoadLibrary`/`GetProcAddress`)运行时动态绑定,免 .lib,缺 DLL 时也能编译 |
| `[HardwareDefAttributes]` + 反射扫描 | 三层枚举自动匹配 | **静态注册表**:`Factory::Register()` + `RegisterBuiltinHardware()` |
| `IHardware` 接口 | 统一抽象 | `canhw::IHardware`(虚函数,5 个成员一一对应) |
| `EventHandler<FrameBase>` | 收发回调 | `std::function<void(const FrameBase&)> eventHandler` |
| `INotifyPropertyChanged`(ModeBase) | WPF 属性绑定 | 省略(纯逻辑层不需要) |
| `IXmlSerializer` + XmlAttributeParser | 配置 XML 持久化 | **暂未移植**(配置为普通结构体) |
| `Task.Run` + `CancellationTokenSource` | 后台读线程 | `std::thread` + `std::atomic<bool>`,`Close` 时 `join()` 防死锁 |
| `throw new Exception(...)` | 错误处理 | `std::runtime_error` / `std::invalid_argument`,文案与 .NET 版一致 |

方法命名与 .NET 版保持一致(`Open/Close/Send/Config/eventHandler`),
数据字段用 camelCase。厂商专属枚举放在各自的 `XxxConfig.h` 中
(设备类型枚举在公共 `Enums.h`,供工厂三层键使用)。

---

## 二、已移植厂商一览(12 家 / 23 个 Wrapper)

| 厂商 | 驱动 DLL | 驱动风格 | 注册键(厂商/设备类型/协议) | Wrapper |
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

驱动 DLL 来源:

- **随工程携带**(`.NET 工程 HwDependency/` 下,运行时按该相对路径或 exe 同级目录解析):
  ZLG、PEAK、Kvaser、创芯、同星、图莫斯、Vector
- **系统安装**(裸 DLL 名按系统搜索路径解析):
  ESD、NI-XNET、Intrepid、EbEl;ETAS 额外查注册表
  `HKLM\SOFTWARE\ETAS\BOA\2\PATH` + Program Files 默认路径回退

---

## 三、工程结构

```
CANHardwareCpp/
├── CMakeLists.txt
├── include/canhw/          对外头文件(使用方只需 include <canhw/Canhw.h>)
│   ├── Canhw.h             伞头文件
│   ├── Enums.h             厂商/协议/设备类型枚举(12 家)
│   ├── Frame.h             FrameBase / CANFrame / BusFrameFlags / CanFrameFlags
│   ├── IHardware.h         统一硬件接口
│   ├── Factory.h           工厂(静态注册表,替代反射)
│   ├── CanFrameHelper.h    SocketCAN 风格 ID 解析
│   ├── DataLenConverter.h  DLC ⇄ 长度换算与校验
│   ├── Sync.h              线程兼容层(无线程模型的 MinGW→Win32 临界区/线程,其余→std)
│   └── XxxConfig.h ×12     各厂商配置结构体 + 厂商专属枚举
├── src/
│   ├── Frame.cpp / Factory.cpp / RegisterBuiltin.cpp / DynamicLibrary.*
│   ├── zlg/       ZlgApi + FlagParser + 2 Wrapper + Register
│   ├── peak/      PeakApi + FlagParser + 2 Wrapper + Register
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
└── demo/main.cpp           演示:列清单 + ZLG/PEAK 收发 demo
```

---

## 四、构建与运行

### 方式 A:CMake(有 cmake 的环境)

```bash
cd CANHardwareCpp
cmake -B build -A x64        # MSVC;MinGW 用 -G "MinGW Makefiles"
cmake --build build --config Release
```

### 方式 B:g++ 直接编译(本机 MinGW 无 cmake 时,已验证全绿)

```bash
cd CANHardwareCpp
mkdir -p build_gcc
for f in $(find src demo -name '*.cpp'); do
  g++ -std=c++17 -Iinclude -Isrc -c "$f" -o "build_gcc/$(echo $f | tr '/' '_').o" || echo "FAIL: $f"
done
g++ build_gcc/*.o -o build_gcc/canhw_demo.exe -ladvapi32
./build_gcc/canhw_demo.exe     # 应列出全部 12 家硬件清单
```

> **工具链说明**:本机 `C:\mingw64`(gcc8)是 win32 线程模型,libstdc++ **没有
> `std::mutex/std::thread`**——参考工程 QC2015P_Cpp_Plus 同样因此改用 Win32 API
> (见其 `main_final.cpp` 注释)。本工程通过 [include/canhw/Sync.h](include/canhw/Sync.h)
> 兼容:该工具链下自动退化为 `CRITICAL_SECTION`/`CreateThread`,MSVC 与
> posix 线程 MinGW 下仍走标准库,上层代码统一使用
> `Mutex / LockGuard / Thread(Start/Join) / SleepMs`。

运行(随工程携带的驱动位于 .NET 工程 `CAN.Hardware/HwDependency/` 下,二选一):

```bash
# 方式 1:在 .NET 工程目录(含 HwDependency\)下运行 C++ 构建产物
cd CAN.Hardware                     # 仓库内的 .NET 工程目录
..\CANHardwareCpp\build\Release\canhw_demo.exe        # 列出硬件清单
..\CANHardwareCpp\build\Release\canhw_demo.exe zlg    # ZLG 经典 CAN 收发

# 方式 2:把 HwDependency\ 整目录拷到 canhw_demo.exe 同级
canhw_demo zlg     # ZLG 经典 CAN(USBCAN-II/2E-U)
canhw_demo zlgfd   # ZLG CAN FD(USBCANFD-200U)
canhw_demo peak    # PEAK 经典 CAN(PCAN-USB)
canhw_demo peakfd  # PEAK CAN FD
```

> 无参数运行只打印**全部 12 家**的已注册硬件清单,不需要任何硬件。
> 收发 demo 需要插对应设备并安装厂商驱动,否则 `Open` 抛出带指导信息的异常
> (如"请先安装 ESD CAN 驱动"/"未发现任何 ETAS CAN 硬件节点")。

---

## 五、使用示例(与 .NET 版三步走一致)

```cpp
#include <canhw/Canhw.h>
using namespace canhw;

int main() {
    RegisterBuiltinHardware();   // 静态库必须先注册(对应 .NET 反射)

    // ① 工厂按三层枚举取实例(12 家用法完全一致)
    auto hw = Factory::GetInstance(HardwareManufacturer::Kvaser,
                                   KvaserDeviceType::Generic, CommProtocol::CAN);

    // ② 配参数(static_cast 到具体配置类型)
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

---

## 六、新增一家厂商(对照 .NET 版 8 步)

1. `include/canhw/Enums.h`:加设备类型枚举(如 `AcmeDeviceType`);
2. `src/Factory.cpp` 的 `ValidateEquipmentType`:登记该厂商枚举(强校验);
3. `src/<vendor>/XxxApi.*`:写 DLL 动态绑定(仿 `ZlgApi`,含原生结构体);
4. 驱动 DLL 放入 `HwDependency/...` 或按系统路径解析,路径写进 `DefaultDllPath()`;
5. `src/<vendor>/XxxFlagParser.h`:原生 flag → 统一 `CanFrameFlags`;
6. `include/canhw/XxxConfig.h`:配置结构体(继承 `BusConfigBase`);
7. `src/<vendor>/XxxWrapper.*`:实现 `IHardware`(锁 + 读线程/回调 + 发送强校验);
8. `XxxRegister.cpp` 注册 + 在 `BuiltinRegistrations.h`、`RegisterBuiltin.cpp`、
   `CMakeLists.txt`、`Canhw.h` 中挂接。

**上层调用方式零改动**——`Factory::GetInstance(...)` 自动找到新注册的封装。

---

## 七、已知差异 / 注意事项

1. **配置持久化(IXmlSerializer/XML)与 WPF 属性通知未移植**(无 GUI 框架依赖是本版目标);
2. ZLG 时间戳按 .NET 参考实现以 **0.1ms → µs(×100)** 换算;Toomoss 经典 100µs×100、
   FD 10µs×10;NI 100ns÷10;Intrepid 双 32 位 tick 浮点换算;EbEl 单位未公开透传——均与 .NET 一致;
3. **同星 Tosun 的事件回调按通道号路由**(回调不带设备上下文):同设备同通道的
   后注册者覆盖先注册者,与 .NET 中第二次 `tscan_register_event_can` 的行为一致;
4. **ETAS 回调以 `userData=this` 定位实例**,实例销毁前先 `Close` 使在途回调失效;
5. `Close()` 统一用 `Thread::Join()` 等待读线程退出(比 .NET 的 `Wait(1000)` 超时更严格,
   各家读循环均在 ≤200ms 内可退出);
6. **线程原语经 `Sync.h` 兼容层**:本机 MinGW(无线程模型)下底层是 Win32
   临界区/`CreateThread`(与 QC2015P_Cpp_Plus 惯例一致),MSVC / posix MinGW 下
   是标准库;业务代码统一用 `Mutex / LockGuard / Thread / SleepMs`;
7. 仅 Windows 完成设计验证(`DynamicLibrary` 预留 POSIX `dlopen` 分支;
   ETAS 注册表定位与 `Sync.h` 的 Win32 分支为 Windows 专属,其他裸名加载可跨平台)。
