#pragma once
#include <cstdint>

namespace canhw {

/// SocketCAN 风格的 CAN ID 标志位定义(对应 .NET CanFrameDef)
namespace CanFrameDef {
constexpr uint32_t CAN_STD_MAX_ID_FLAG = 0x7FF;
constexpr uint32_t CAN_EFF_FLAG = 0x80000000U; /* 扩展帧标志,合成到 ID bit31 */
constexpr uint32_t CAN_ID_FLAG  = 0x1FFFFFFFU; /* 真实 ID 位 */
constexpr uint32_t CAN_RTR_FLAG = 0x40000000U; /* 远程帧标志 */
constexpr uint32_t CAN_ERR_FLAG = 0x20000000U; /* 错误帧标志 */
} // namespace CanFrameDef

/// 从原始 ID 中解析扩展帧 / RTR / 错误帧,并剥离真实 ID(对应 .NET CanFrameHelper)
class CanFrameHelper {
public:
    static bool IsEFF(int64_t id) {
        if ((static_cast<uint32_t>(id) & CanFrameDef::CAN_EFF_FLAG) != 0) return true;
        return GetId(id) > CanFrameDef::CAN_STD_MAX_ID_FLAG;
    }

    static bool IsRTR(int64_t id) {
        return (static_cast<uint32_t>(id) & CanFrameDef::CAN_RTR_FLAG) != 0;
    }

    static bool IsERR(int64_t id) {
        return (static_cast<uint32_t>(id) & CanFrameDef::CAN_ERR_FLAG) != 0;
    }

    static uint32_t GetId(int64_t id) {
        return static_cast<uint32_t>(id) & CanFrameDef::CAN_ID_FLAG;
    }
};

} // namespace canhw
