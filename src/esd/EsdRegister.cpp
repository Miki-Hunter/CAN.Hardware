// ESD 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "EsdCanFdWrapper.h"
#include "EsdCanWrapper.h"

namespace canhw {

void RegisterEsdHardware() {
    Factory::Register(HardwareManufacturer::Esd, EsdDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<esd::EsdCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Esd, EsdDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<esd::EsdCanFdWrapper>(); });
}

} // namespace canhw
