#pragma once
// ==================================================================
//  创芯科技标志位解析器(对应 .NET Comm/ChuangXinCanFlagParser.cs
//  与 ChuangXinCanFdFlagParser.cs)
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/CanFrameHelper.h>
#include <canhw/Frame.h>

namespace canhw {
namespace chuangxin {

/// 经典 CAN(ControlCAN VCI)解析器。
/// VCI_CAN_OBJ 的 ID 不含标志位,扩展/远程帧由 ExternFlag/RemoteFlag 表示;
/// 统一合成为 SocketCAN 风格 ID。
class ChuangXinCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t id, uint8_t remoteFlag,
                                                      uint8_t externFlag) {
        const bool isExt = externFlag != 0;
        const bool isRtr = remoteFlag != 0;

        uint32_t rawId = id & CanFrameDef::CAN_ID_FLAG;
        if (isExt) rawId |= CanFrameDef::CAN_EFF_FLAG;
        if (isRtr) rawId |= CanFrameDef::CAN_RTR_FLAG;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = rawId;
        f->isRTR = isRtr;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        // 经典 CAN 通道无 FD 属性
        f->isCanFd = false;
        f->hasBrs = false;
        f->hasEsi = false;
        return f;
    }
};

/// CANFD(ControlCANFD ZCAN)解析器。
/// can_id 高位为 SocketCAN 风格标志;BRS/ESI 在 canfd_frame.flags:bit0/bit1。
class ChuangXinCanFdFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t rawCanId, uint8_t fdFlags,
                                                      bool isFd) {
        constexpr uint8_t CANFD_BRS = 0x01;
        constexpr uint8_t CANFD_ESI = 0x02;

        const bool isExt = (rawCanId & CanFrameDef::CAN_EFF_FLAG) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = rawCanId;
        f->isRTR = (rawCanId & CanFrameDef::CAN_RTR_FLAG) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isErrorFrame = (rawCanId & CanFrameDef::CAN_ERR_FLAG) != 0;
        f->isCanFd = isFd;
        f->hasBrs = isFd && (fdFlags & CANFD_BRS) != 0;
        f->hasEsi = isFd && (fdFlags & CANFD_ESI) != 0;
        return f;
    }
};

} // namespace chuangxin
} // namespace canhw
