// 图莫斯 Toomoss 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "ToomossCanFdWrapper.h"
#include "ToomossCanWrapper.h"

namespace canhw {

void RegisterToomossHardware() {
    Factory::Register(HardwareManufacturer::Toomoss, ToomossDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<toomoss::ToomossCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Toomoss, ToomossDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<toomoss::ToomossCanFdWrapper>(); });
}

} // namespace canhw
