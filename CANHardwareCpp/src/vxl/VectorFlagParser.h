#pragma once
// ==================================================================
//  Vector CAN/CANFD 标志位解析器(对应 .NET Comm/VectorCanFlagParser.cs)
//  经典 CAN 使用 XLevent.msgFlags;CANFD 使用 XLcanRxEvent.msgFlags;
//  扩展帧标志不在 flags 中,位于原始 ID 的 bit31(XL_CAN_EXT_MSG_ID)。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "VectorApi.h"

namespace canhw {
namespace vxl {

class VectorCanFlagParser {
public:
    /// 经典 CAN(XLevent.msgFlags + 原始 ID)
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t msgFlags,
                                                      uint32_t rawId) {
        const bool has = [&](uint16_t f) { return (msgFlags & f) != 0; };
        const bool isExt = (rawId & XL_CAN_EXT_MSG_ID) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = msgFlags;
        f->isRTR = has(MsgFlags::REMOTE_FRAME);
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isWakeup = has(MsgFlags::WAKEUP);
        f->isNerr = has(MsgFlags::NERR);
        f->isTxAck = has(MsgFlags::TX_COMPLETED);
        f->isTxRequest = has(MsgFlags::TX_REQUEST);
        f->isErrorFrame = has(MsgFlags::ERROR_FRAME);
        f->hasOverrun = has(MsgFlags::OVERRUN);
        return f;
    }

    /// CAN FD(XLcanRxEvent.msgFlags + 原始 ID);isTxAck 表示发送确认事件
    static std::shared_ptr<const CanFrameFlags> ParseFd(uint32_t msgFlags,
                                                        uint32_t rawId,
                                                        bool isTxAck = false) {
        const bool isExt = (rawId & XL_CAN_EXT_MSG_ID) != 0;
        const bool isError = (msgFlags & FdRxFlags::EF) != 0;
        const bool isTe = (msgFlags & FdRxFlags::TE) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = msgFlags;
        f->isRTR = (msgFlags & FdRxFlags::RTR) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isWakeup = (msgFlags & FdRxFlags::WAKEUP) != 0;
        f->isCanFd = (msgFlags & FdRxFlags::EDL) != 0;
        f->hasBrs = (msgFlags & FdRxFlags::BRS) != 0;
        f->hasEsi = (msgFlags & FdRxFlags::ESI) != 0;
        f->isTxAck = isTxAck;
        f->isArbitrationLost = (msgFlags & FdRxFlags::ARB_LOST) != 0;
        f->isErrorFrame = isError;
        f->hasBusError = isError || isTe;
        return f;
    }
};

} // namespace vxl
} // namespace canhw
