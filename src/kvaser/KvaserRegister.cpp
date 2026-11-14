// Kvaser 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "KvaserCanFDWrapper.h"
#include "KvaserCanWrapper.h"

namespace canhw {

void RegisterKvaserHardware() {
    Factory::Register(HardwareManufacturer::Kvaser, KvaserDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<kvaser::KvaserCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Kvaser, KvaserDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<kvaser::KvaserCanFDWrapper>(); });
}

} // namespace canhw
