#pragma once
// ==================================================================
//  Kvaser canRead() flag 解析器(对应 .NET Comm/KvaserCanFlagParser.cs
//  + KvaserMessageFlags.cs)
//  布局:Bit 0-7 canMSG_xxx,Bit 8-15 canMSGERR_xxx,
//       Bit 16-23 canFDMSG_xxx,Bit 24-31 扩展标志。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

namespace canhw {
namespace kvaser {

/// Kvaser flag 位定义(对应 .NET KvaserMessageFlags)
namespace KvaserMessageFlags {
constexpr uint32_t Rtr = 0x0001;
constexpr uint32_t Std = 0x0002;
constexpr uint32_t Ext = 0x0004;
constexpr uint32_t Wakeup = 0x0008;
constexpr uint32_t Nerr = 0x0010;
constexpr uint32_t ErrorFrame = 0x0020;
constexpr uint32_t TxAck = 0x0040;
constexpr uint32_t TxRq = 0x0080;
constexpr uint32_t DelayMsg = 0x0100;

constexpr uint32_t SingleShot = 0x01000000;
constexpr uint32_t TxNack = 0x02000000;
constexpr uint32_t Abl = 0x04000000;
constexpr uint32_t LocalTxAck = 0x10000000;

constexpr uint32_t HwOverrun = 0x0200;
constexpr uint32_t SwOverrun = 0x0400;
constexpr uint32_t Stuff = 0x0800;
constexpr uint32_t Form = 0x1000;
constexpr uint32_t Crc = 0x2000;
constexpr uint32_t Bit0 = 0x4000;
constexpr uint32_t Bit1 = 0x8000;

constexpr uint32_t Fdf = 0x010000;
constexpr uint32_t Brs = 0x020000;
constexpr uint32_t Esi = 0x040000;

constexpr uint32_t Overrun = HwOverrun | SwOverrun;
constexpr uint32_t BitError = Bit0 | Bit1;
constexpr uint32_t BusError = Stuff | Form | Crc | Bit0 | Bit1;
} // namespace KvaserMessageFlags

class KvaserCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t rawFlag) {
        using namespace KvaserMessageFlags;
        const bool has = [&](uint32_t f) { return (rawFlag & f) == f; };

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = rawFlag;

        f->isRTR = has(Rtr);
        f->isStandardFrame = has(Std);
        f->isExtendedFrame = has(Ext);
        f->isWakeup = has(Wakeup);
        f->isNerr = has(Nerr);

        f->isCanFd = has(Fdf);
        f->hasBrs = has(Brs);
        f->hasEsi = has(Esi);

        f->isTxAck = has(TxAck);
        f->isTxRequest = has(TxRq);
        f->isLocalEcho = has(LocalTxAck);
        f->isSingleShot = has(SingleShot);
        f->isTxFailed = has(TxNack);
        f->isArbitrationLost = has(Abl);
        f->isDelayMessage = has(DelayMsg);

        f->isErrorFrame = has(ErrorFrame);
        f->hasHwOverrun = has(HwOverrun);
        f->hasSwOverrun = has(SwOverrun);
        f->hasOverrun = (rawFlag & Overrun) != 0;
        f->hasStuffError = has(Stuff);
        f->hasFormError = has(Form);
        f->hasCrcError = has(Crc);
        f->hasBitError = (rawFlag & BitError) != 0;
        f->hasBusError = (rawFlag & BusError) != 0;

        return f;
    }
};

} // namespace kvaser
} // namespace canhw
