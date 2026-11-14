// 创芯科技硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "ChuangXinCanFdWrapper.h"
#include "ChuangXinCanWrapper.h"

namespace canhw {

void RegisterChuangXinHardware() {
    // 经典 CAN 分析仪(USBCAN-2A/2C、CANalyst-II 系列,ControlCAN.dll)
    Factory::Register(HardwareManufacturer::ChuangXin, ChuangXinDeviceType::USBCAN,
                      CommProtocol::CAN,
                      [] { return std::make_unique<chuangxin::ChuangXinCanWrapper>(); });

    // USBCANFD 分析仪(200U/100U/MINI,ControlCANFD.dll):双协议键
    Factory::Register(HardwareManufacturer::ChuangXin, ChuangXinDeviceType::USBCANFD,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<chuangxin::ChuangXinCanFdWrapper>(); });
    Factory::Register(HardwareManufacturer::ChuangXin, ChuangXinDeviceType::USBCANFD,
                      CommProtocol::CAN,
                      [] { return std::make_unique<chuangxin::ChuangXinCanFdWrapper>(); });
}

} // namespace canhw
