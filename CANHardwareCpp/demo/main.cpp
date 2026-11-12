// ==================================================================
//  CANHardwareCpp 演示程序
//  用法:
//    canhw_demo              列出已注册的硬件清单(无需硬件)
//    canhw_demo zlg          ZLG 经典 CAN 收发演示(需 USBCAN 设备 + 驱动)
//    canhw_demo zlgfd        ZLG CAN FD 演示(需 USBCANFD-200U)
//    canhw_demo peak         PEAK 经典 CAN 演示(需 PCAN-USB)
//    canhw_demo peakfd       PEAK CAN FD 演示(需 FD 设备)
//    canhw_demo peak2ch      PEAK 双通道互连演示(USBBUS1 与 USBBUS2 互连)
// ==================================================================
#include <canhw/Canhw.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <canhw/Sync.h>

using namespace canhw;

namespace {

void PrintHardwareMap() {
    std::cout << "Registered hardware (厂商 -> 设备类型 -> 协议):\n";
    for (const auto& manufacturerEntry : Factory::GetHardwareMap()) {
        std::cout << "  " << ToString(manufacturerEntry.first) << ":\n";
        for (const auto& typeEntry : manufacturerEntry.second) {
            std::cout << "    " << EquipmentTypeName(manufacturerEntry.first,
                                                     typeEntry.first)
                      << " ->";
            for (const auto protocol : typeEntry.second) {
                std::cout << " " << ToString(protocol);
            }
            std::cout << '\n';
        }
    }
    std::cout << std::endl;
}

// 多通道演示时各读线程并发回调,整帧打印需持锁避免交错
canhw::Mutex gPrintMutex;

void PrintFrame(const FrameBase& frame, const char* prefix = "") {
    const auto* canFrame = dynamic_cast<const CANFrame*>(&frame);
    if (canFrame == nullptr) return;

    canhw::LockGuard guard(gPrintMutex);
    std::cout << prefix << (canFrame->dir == 0 ? "TX" : "RX") << " 0x" << std::hex
              << std::uppercase << std::setw(3) << std::setfill('0') << canFrame->id
              << std::dec << " DLC=" << canFrame->dlc << " [";
    for (int i = 0; i < canFrame->dlc; ++i) {
        if (i > 0) std::cout << ' ';
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<int>(canFrame->data[static_cast<size_t>(i)]);
    }
    std::cout << std::dec << ']';
    if (canFrame->flags) std::cout << ' ' << canFrame->flags->ToString();
    std::cout << std::endl;
}

// 双通道互连演示:ch1(PCAN_USBBUS1)与 ch2(PCAN_USBBUS2)互连,
// 各发一帧,除各自 TX 回显外还能在对端看到 RX(验证跨通道接收)
void DemoPeakTwoChannels() {
    auto hw1 = Factory::GetInstance(HardwareManufacturer::Peak, PeakDeviceType::Generic,
                                    CommProtocol::CAN);
    auto hw2 = Factory::GetInstance(HardwareManufacturer::Peak, PeakDeviceType::Generic,
                                    CommProtocol::CAN);
    if (!hw1 || !hw2) {
        std::cerr << "Peak not registered\n";
        return;
    }

    auto& cfg1 = static_cast<PeakCANConfig&>(hw1->Config());
    cfg1.canDeviceChannel = 0x51;  // PCAN_USBBUS1
    cfg1.canBitrate = PeakCanBitrate::pcanBAUD_500K;
    auto& cfg2 = static_cast<PeakCANConfig&>(hw2->Config());
    cfg2.canDeviceChannel = 0x52;  // PCAN_USBBUS2
    cfg2.canBitrate = PeakCanBitrate::pcanBAUD_500K;

    // 各通道的收发(含回显)都带通道前缀打印(前缀与帧同锁,防交错)
    hw1->eventHandler = [](const FrameBase& frame) { PrintFrame(frame, "[ch1] "); };
    hw2->eventHandler = [](const FrameBase& frame) { PrintFrame(frame, "[ch2] "); };

    std::cout << "Opening PEAK ch1(0x51) + ch2(0x52) ..." << std::endl;
    hw1->Open();
    hw2->Open();
    std::cout << "Opened. ch1 sends 0x123, ch2 sends 0x456, listen 2s ..."
              << std::endl;

    CANFrame frame1;
    frame1.id = 0x123;
    frame1.dlc = 8;
    frame1.data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    hw1->Send(frame1);

    CANFrame frame2;
    frame2.id = 0x456;
    frame2.dlc = 8;
    frame2.data = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};
    hw2->Send(frame2);

    canhw::SleepMs(2000);

    hw1->Close();
    hw2->Close();
    std::cout << "Closed." << std::endl;
}

// 三步走:取实例 → 配参数 → 订阅/收发(与 .NET 版用法一致)
void RunDemo(const std::string& which) {
    std::unique_ptr<IHardware> hardware;
    std::string name;

    if (which == "zlg") {
        hardware = Factory::GetInstance(HardwareManufacturer::Zlg,
                                        ZlgDeviceType::USBCAN, CommProtocol::CAN);
        name = "ZLG USBCAN";
        if (hardware) {
            auto& config = static_cast<ZlgCANConfig&>(hardware->Config());
            config.deviceChannel = 0;                 // 通道号
            config.canBitrate = ZlgCanBitrate::_500K; // 波特率 500K
        }
    } else if (which == "zlgfd") {
        hardware = Factory::GetInstance(HardwareManufacturer::Zlg,
                                        ZlgDeviceType::USBCANFD200U,
                                        CommProtocol::CANFD);
        name = "ZLG USBCANFD-200U";
        if (hardware) {
            auto& config = static_cast<ZlgCanFDConfig&>(hardware->Config());
            config.deviceChannel = 0;
            config.useCanFD = true;
            config.arbitrationBitrate = ZlgCanBitrate::_500K; // 仲裁段 500K
            config.dataBitrate = ZlgCanFDBitrate::_2M_80P;    // 数据段 2M
        }
    } else if (which == "peak") {
        hardware = Factory::GetInstance(HardwareManufacturer::Peak,
                                        PeakDeviceType::Generic, CommProtocol::CAN);
        name = "PEAK PCAN";
        if (hardware) {
            auto& config = static_cast<PeakCANConfig&>(hardware->Config());
            config.canDeviceChannel = 0x51; // PCAN_USBBUS1
            config.canBitrate = PeakCanBitrate::pcanBAUD_500K;
        }
    } else if (which == "peakfd") {
        hardware = Factory::GetInstance(HardwareManufacturer::Peak,
                                        PeakDeviceType::Generic, CommProtocol::CANFD);
        name = "PEAK PCAN FD";
        if (hardware) {
            auto& config = static_cast<PeakCanFDConfig&>(hardware->Config());
            config.canFdDeviceChannel = 0x51; // PCAN_USBBUS1
            // 默认位时序即 500K(仲裁段)/2M(数据段),可按需修改
        }
    } else {
        std::cerr << "Unknown demo: " << which << "\n";
        return;
    }

    if (!hardware) {
        std::cerr << "Hardware not registered: " << which << "\n";
        return;
    }

    // ③ 订阅报文(接收与发送回显都会推到这里)
    hardware->eventHandler = [](const FrameBase& frame) { PrintFrame(frame); };

    std::cout << "Opening " << name << " ..." << std::endl;
    if (!hardware->Open()) {
        std::cerr << "Open failed\n";
        return;
    }
    std::cout << "Opened. Sending one frame, listen 2s ..." << std::endl;

    // ④ 发送一帧(经典 CAN;FD 示例见下方注释)
    CANFrame frame;
    frame.id = 0x123;
    frame.dlc = 8;
    frame.data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    hardware->Send(frame);

    // CAN FD 发送示例(把上面换成):
    // frame.dlc = 64; frame.fdf = true; frame.brs = true;
    // frame.data.resize(64); for (size_t i = 0; i < 64; ++i) frame.data[i] = (uint8_t)i;

    canhw::SleepMs(2000);

    hardware->Close();
    std::cout << "Closed." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    // 注册内置硬件(静态库模式必须先调用,对应 .NET 的反射自动注册)
    RegisterBuiltinHardware();

    PrintHardwareMap();

    if (argc < 2) {
        std::cout << "Usage: canhw_demo [zlg|zlgfd|peak|peakfd|peak2ch]\n"
                     "  (收发演示需要对应硬件与厂商驱动;\n"
                     "   驱动 DLL 位于 HwDependency/ 下,请在包含该目录的位置运行,\n"
                     "   或把 HwDependency 放到可执行文件同目录)\n";
        return 0;
    }

    try {
        if (std::string(argv[1]) == "peak2ch")
            DemoPeakTwoChannels();
        else
            RunDemo(argv[1]);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
