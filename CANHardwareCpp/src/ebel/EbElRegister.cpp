// EbEl(Eberspächer Electronics)硬件注册
// (对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性;该厂商仅经典 CAN)
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "EbElCanWrapper.h"

namespace canhw {

void RegisterEbElHardware() {
    // 注意:EbEl(FlexCard)只有经典 CAN,无 CAN FD(12 家里唯一)
    Factory::Register(HardwareManufacturer::EbEl, EbElDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<ebel::EbElCanWrapper>(); });
}

} // namespace canhw
