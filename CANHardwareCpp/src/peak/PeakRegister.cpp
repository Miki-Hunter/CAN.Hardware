// PEAK(PCAN-Basic)硬件注册
// 对应 .NET 版由反射读取 Wrapper 类上的 [HardwareDefAttributes] 特性自动完成的注册
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "PeakCanFdWrapper.h"
#include "PeakCanWrapper.h"

namespace canhw {

void RegisterPeakHardware() {
    Factory::Register(HardwareManufacturer::Peak, PeakDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<peak::PeakCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Peak, PeakDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<peak::PeakCanFdWrapper>(); });
}

} // namespace canhw
