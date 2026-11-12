#pragma once
// ==================================================================
//  同星(ToSun) CAN/CANFD 标志位解析器(对应 .NET Comm/ToSunCanFlagParser.cs)
//  FProperties:bit0 Tx, bit1 Remote, bit2 Ext, bit6 Logged, bit7 Error;
//  FFDProperties:bit0 FD, bit1 BRS, bit2 ESI。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "TosunApi.h"

namespace canhw {
namespace tosun {

class ToSunCanFlagParser {
public:
    /// 经典 CAN(TLIBCAN.FProperties)
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t fProperties) {
        return Parse(fProperties, 0);
    }

    /// CAN FD(TLIBCANFD.FProperties + FFDProperties)
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t fProperties,
                                                      uint32_t fdProperties) {
        const bool isExt = (fProperties & PROP_EXT) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = fProperties | (fdProperties << 8);
        f->isRTR = (fProperties & PROP_REMOTE) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isTxAck = (fProperties & PROP_TX) != 0;
        f->isCanFd = (fdProperties & FD_PROP_FDF) != 0;
        f->hasBrs = (fdProperties & FD_PROP_BRS) != 0;
        f->hasEsi = (fdProperties & FD_PROP_ESI) != 0;
        f->isErrorFrame = (fProperties & PROP_ERROR) != 0;
        return f;
    }
};

} // namespace tosun
} // namespace canhw
