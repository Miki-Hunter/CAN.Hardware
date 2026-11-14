#pragma once
// ==================================================================
//  EbEl(FlexCard)CAN 标志位解析器(对应 .NET Comm/EbElCanFlagParser.cs)
//  将 fcCANPacket 的 ID(bit29=扩展帧)与 FlagsAndDLC 字节
//  (bit4=方向, bit5=RTR, bit6=丢帧)解析为统一 CanFrameFlags。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "EbElApi.h"

namespace canhw {
namespace ebel {

class EbElCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t rawId,
                                                      uint8_t flagsAndDlc) {
        const bool isTxAck = (flagsAndDlc & DlcFlags::Direction) != 0;
        const bool messageLost = (flagsAndDlc & DlcFlags::MessageLost) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = flagsAndDlc;
        f->isRTR = (flagsAndDlc & DlcFlags::RemoteFrame) != 0;
        f->isStandardFrame = (rawId & FC_EXT_FLAG) == 0;
        f->isExtendedFrame = (rawId & FC_EXT_FLAG) != 0;
        f->isTxAck = isTxAck;
        f->hasOverrun = messageLost;
        f->hasHwOverrun = messageLost;
        return f;
    }
};

} // namespace ebel
} // namespace canhw
