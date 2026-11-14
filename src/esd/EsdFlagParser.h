#pragma once
// ==================================================================
//  ESD CAN/CANFD 标志位解析器(对应 .NET Comm/EsdCanFlagParser.cs)
//  帧属性集中在 BaseMsg.Len 字节与原始 ID 的 bit29(扩展帧);
//  ESI 在 BaseMsg.Esi,丢帧计数在 BaseMsg.MsgLost。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "EsdApi.h"

namespace canhw {
namespace esd {

class EsdCanFlagParser {
public:
    /// 解析 ntcan 消息头为统一标志位。rawId 为驱动原始 ID(含 bit29 扩展标记)
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t rawId, uint8_t len,
                                                      uint8_t esi, uint8_t msgLost) {
        const bool isExt = (rawId & NTCAN_EXT_FLAG) != 0;
        const bool isFd = (len & LEN_FLAG_FD) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = len;
        // FD 帧内 bit4 复用为 NO_BRS(置位=不加速),经典帧才是 RTR
        f->isRTR = !isFd && (len & LEN_FLAG_RTR) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isCanFd = isFd;
        f->hasBrs = isFd && (len & LEN_FLAG_FD_NO_BRS) == 0;
        f->hasEsi = esi != 0;
        // 驱动上报的丢帧计数映射为溢出
        f->hasOverrun = msgLost != 0;
        return f;
    }
};

} // namespace esd
} // namespace canhw
