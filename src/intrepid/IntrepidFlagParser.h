#pragma once
// ==================================================================
//  Intrepid CAN/CANFD 标志位解析器(对应 .NET Comm/IntrepidCanFlagParser.cs)
//  将 icsSpyMessage 的 StatusBitField/2/3 解析为统一 CanFrameFlags。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "IntrepidApi.h"

namespace canhw {
namespace intrepid {

class IntrepidCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t statusBitField,
                                                      uint32_t statusBitField2,
                                                      uint32_t statusBitField3) {
        const auto has1 = [&](uint32_t flag) {
            return (statusBitField & flag) == flag;
        };
        const auto has2 = [&](uint32_t flag) {
            return (statusBitField2 & flag) == flag;
        };
        const auto has3 = [&](uint32_t flag) {
            return (statusBitField3 & flag) == flag;
        };

        const bool hasCrc = has1(StatusBits::CRC_ERROR) || has1(StatusBits::CHECKSUM_ERROR);
        const bool busOff = has1(StatusBits::CAN_BUS_OFF);
        const bool errorPassive = has1(StatusBits::CAN_ERROR_PASSIVE);
        const bool errorWarning = has1(StatusBits::CAN_ERROR_WARNING);

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = statusBitField;
        f->isRTR = has1(StatusBits::REMOTE_FRAME);
        f->isStandardFrame = !has1(StatusBits::XTD_FRAME);
        f->isExtendedFrame = has1(StatusBits::XTD_FRAME);
        f->isCanFd = has3(StatusBits3::CANFD_FDF);
        f->hasBrs = has3(StatusBits3::CANFD_BRS);
        f->hasEsi = has3(StatusBits3::CANFD_ESI);
        f->isTxAck = has1(StatusBits::TX_MSG);
        f->isErrorFrame =
            has1(StatusBits::GLOBAL_ERR) || has2(StatusBits2::ERROR_FRAME) || busOff;
        f->hasSwOverrun = has1(StatusBits::COMM_IN_OVERFLOW);
        f->hasOverrun = has1(StatusBits::COMM_IN_OVERFLOW);
        f->hasCrcError = hasCrc;
        f->hasBusError = hasCrc || busOff || errorPassive || errorWarning;
        return f;
    }
};

} // namespace intrepid
} // namespace canhw
