// 同星 Tosun 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "TosunCanFdWrapper.h"
#include "TosunCanWrapper.h"

namespace canhw {

void RegisterTosunHardware() {
    Factory::Register(HardwareManufacturer::Tosun, TosunDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<tosun::ToSunCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Tosun, TosunDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<tosun::ToSunCanFdWrapper>(); });
}

} // namespace canhw
