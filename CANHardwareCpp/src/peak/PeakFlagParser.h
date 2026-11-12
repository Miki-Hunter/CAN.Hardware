#pragma once
// ==================================================================
//  PEAK(PCAN-Basic)CAN/CANFD 标志位解析器
//  (对应 .NET Comm/PeakCanFlagParser.cs)
//  输入为 TPCANMessageType(MSGTYPE)的原始值。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "PeakApi.h"

namespace canhw {
namespace peak {

class PeakFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t msgType) {
        auto f = std::make_shared<CanFrameFlags>();

        const bool has = [&](uint8_t flag) { return (msgType & flag) == flag; };
        const bool isExt = has(PCAN_MESSAGE_EXTENDED);

        f->rawFlags = msgType;
        f->isRTR = has(PCAN_MESSAGE_RTR);
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isCanFd = has(PCAN_MESSAGE_FD);
        f->hasBrs = has(PCAN_MESSAGE_BRS);
        f->hasEsi = has(PCAN_MESSAGE_ESI);
        f->isErrorFrame = has(PCAN_MESSAGE_ERRFRAME);

        return f;
    }
};

} // namespace peak
} // namespace canhw
