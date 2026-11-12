// Intrepid 英特佩斯硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "IntrepidCanFdWrapper.h"
#include "IntrepidCanWrapper.h"

namespace canhw {

void RegisterIntrepidHardware() {
    Factory::Register(HardwareManufacturer::Intrepid, IntrepidDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<intrepid::IntrepidCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Intrepid, IntrepidDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] {
                          return std::make_unique<intrepid::IntrepidCanFdWrapper>();
                      });
}

} // namespace canhw
