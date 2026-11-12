// ETAS 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "EtasCanFdWrapper.h"
#include "EtasCanWrapper.h"

namespace canhw {

void RegisterEtasHardware() {
    Factory::Register(HardwareManufacturer::Etas, EtasDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<etas::EtasCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Etas, EtasDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<etas::EtasCanFdWrapper>(); });
}

} // namespace canhw
