#pragma once
// ==================================================================
//  ETAS BOA CAN/CANFD 标志位解析器(对应 .NET Comm/EtasCanFlagParser.cs)
//  输入为 OCI 消息的 MsgFlags(u16)原始值。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/Frame.h>

#include "EtasBoaApi.h"

namespace canhw {
namespace etas {

class EtasCanFlagParser {
public:
    static std::shared_ptr<const CanFrameFlags> Parse(uint16_t msgFlags) {
        const bool isExt = (msgFlags & MSG_FLAG_EXTENDED) != 0;

        auto f = std::make_shared<CanFrameFlags>();
        f->rawFlags = msgFlags;
        f->isRTR = (msgFlags & MSG_FLAG_REMOTE_FRAME) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isCanFd = (msgFlags & MSG_FLAG_FD_DATA) != 0;
        f->hasBrs = (msgFlags & MSG_FLAG_FD_DATA_BIT_RATE) != 0;
        f->hasEsi = (msgFlags & MSG_FLAG_FD_ERROR_PASSIVE) != 0;
        f->isLocalEcho = (msgFlags & MSG_FLAG_SELFRECEPTION) != 0;
        return f;
    }
};

} // namespace etas
} // namespace canhw
