// NI-XNET 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "NiCanFdWrapper.h"
#include "NiCanWrapper.h"

namespace canhw {

void RegisterNiHardware() {
    Factory::Register(HardwareManufacturer::Ni, NiDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<ni::NiCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Ni, NiDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<ni::NiCanFdWrapper>(); });
}

} // namespace canhw
