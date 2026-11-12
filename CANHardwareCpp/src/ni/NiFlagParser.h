#pragma once
// ==================================================================
//  NI-XNET CAN/CANFD 标志位解析器(对应 .NET Comm/NiXnetCanFlagParser.cs)
//  输入为 nxFrame_t 帧头中的 Identifier/Type/Flags 原始值。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "NiApi.h"

namespace canhw {
namespace ni {

class NiXnetCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t identifier,
                                                      uint8_t frameType,
                                                      uint8_t frameFlags) {
        const bool isExt = (identifier & NX_CAN_EXT_FLAG) != 0;
        const bool isEcho = (frameFlags & NX_FRAME_FLAG_TRANSMIT_ECHO) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        // 原始值合成:高 3 位为 Identifier 标志位(bit29=扩展帧),次高字节为 Flags,低字节为 Type
        f->rawFlags = (identifier & 0xE0000000u) |
                      (static_cast<uint32_t>(frameFlags) << 8) | frameType;
        f->isRTR = frameType == NX_FRAME_TYPE_CAN_REMOTE;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isCanFd = (frameType & NX_FRAME_TYPE_CANFD_DATA) != 0;
        f->hasBrs = (frameType & NX_FRAME_TYPE_CANFDBRS_DATA) ==
                    NX_FRAME_TYPE_CANFDBRS_DATA;
        f->isErrorFrame = frameType == NX_FRAME_TYPE_CAN_BUS_ERROR;
        f->isTxAck = isEcho;
        f->isLocalEcho = isEcho;
        return f;
    }
};

} // namespace ni
} // namespace canhw
