// ZLG(周立功)硬件注册
// 对应 .NET 版由反射读取 Wrapper 类上的 [HardwareDefAttributes] 特性自动完成的注册
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "ZlgCanFdWrapper.h"
#include "ZlgUsbCanWrapper.h"

namespace canhw {

void RegisterZlgHardware() {
    // 经典 CAN 设备(USBCAN-II / USBCAN-2E-U 等)
    Factory::Register(HardwareManufacturer::Zlg, ZlgDeviceType::USBCAN,
                      CommProtocol::CAN,
                      [] { return std::make_unique<zlg::ZlgUsbCanWrapper>(); });

    // USBCANFD-200U:同一封装按 useCanFD 配置同时支持 CANFD 与经典 CAN
    Factory::Register(HardwareManufacturer::Zlg, ZlgDeviceType::USBCANFD200U,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<zlg::ZlgCanFdWrapper>(); });
    Factory::Register(HardwareManufacturer::Zlg, ZlgDeviceType::USBCANFD200U,
                      CommProtocol::CAN,
                      [] { return std::make_unique<zlg::ZlgCanFdWrapper>(); });
}

} // namespace canhw
