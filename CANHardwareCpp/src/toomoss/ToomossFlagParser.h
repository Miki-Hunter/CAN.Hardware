#pragma once
// ==================================================================
//  图莫斯标志位解析器(对应 .NET Comm/ToomossCanFlagParser.cs
//  与 ToomossCanFdFlagParser.cs)
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/CanFrameHelper.h>
#include <canhw/Frame.h>

#include "ToomossApi.h"

namespace canhw {
namespace toomoss {

/// 经典 CAN(CAN_MSG)解析器。
/// CAN_MSG.ID 为裸 ID;RemoteFlag bit0=RTR,bit7=发送帧标志;
/// ExternFlag:0x01=扩展帧,0x80=错误帧。
class ToomossCanFlagParser {
public:
    static constexpr uint8_t RemoteFlagRtr = 0x01;
    static constexpr uint8_t RemoteFlagTx = 0x80;
    static constexpr uint8_t ExternFlagExt = 0x01;
    static constexpr uint8_t ExternFlagErr = 0x80;

    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t id, uint8_t remoteFlag,
                                                      uint8_t externFlag, bool isTx) {
        const bool isExt = (externFlag & ExternFlagExt) != 0;
        const bool isErr = (externFlag & ExternFlagErr) != 0;
        const bool isRtr = (remoteFlag & RemoteFlagRtr) != 0;

        uint32_t rawId = id & CanFrameDef::CAN_ID_FLAG;
        if (isExt) rawId |= CanFrameDef::CAN_EFF_FLAG;
        if (isRtr) rawId |= CanFrameDef::CAN_RTR_FLAG;
        if (isErr) rawId |= CanFrameDef::CAN_ERR_FLAG;

        auto f = std::make_shared<CanFrameFlags>();
        // 设备原始标志分散在两个字节中,这里打包保留
        f->rawFlags = (static_cast<uint32_t>(externFlag) << 8) | remoteFlag;
        f->isRTR = isRtr;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isErrorFrame = isErr;
        f->isTxRequest = isTx;
        // 经典 CAN 通道无 FD 属性
        f->isCanFd = false;
        f->hasBrs = false;
        f->hasEsi = false;
        return f;
    }
};

/// CANFD(CANFD_MSG)解析器。
/// ID:bit31=IDE, bit30=RTR, bit28..0=ID;
/// Flags:bit0=BRS, bit1=ESI, bit2=FDF, bit7=发送帧标志。
class ToomossCanFdFlagParser {
public:
    static constexpr uint32_t IdFlagIde = 0x80000000;
    static constexpr uint32_t IdFlagRtr = 0x40000000;
    static constexpr uint32_t IdFlagMask = 0x1FFFFFFF;

    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t id, uint8_t flags,
                                                      bool isTx) {
        const bool isExt = (id & IdFlagIde) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = flags;
        f->isRTR = (id & IdFlagRtr) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isTxRequest = isTx;
        f->isCanFd = (flags & CANFD_FLAG_FDF) != 0;
        f->hasBrs = (flags & CANFD_FLAG_BRS) != 0;
        f->hasEsi = (flags & CANFD_FLAG_ESI) != 0;
        return f;
    }
};

} // namespace toomoss
} // namespace canhw
