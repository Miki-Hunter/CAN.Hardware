// Vector 硬件注册(对应 .NET Wrapper 类上的 [HardwareDefAttributes] 特性)
// 注意:命名空间用 vxl 避免与 std::vector 冲突
#include "BuiltinRegistrations.h"

#include <memory>

#include <canhw/Factory.h>

#include "VectorCanFdWrapper.h"
#include "VectorCanWrapper.h"

namespace canhw {

void RegisterVectorHardware() {
    Factory::Register(HardwareManufacturer::Vector, VectorDeviceType::Generic,
                      CommProtocol::CAN,
                      [] { return std::make_unique<vxl::VectorCanWrapper>(); });
    Factory::Register(HardwareManufacturer::Vector, VectorDeviceType::Generic,
                      CommProtocol::CANFD,
                      [] { return std::make_unique<vxl::VectorCanFdWrapper>(); });
}

} // namespace canhw
