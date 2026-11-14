#pragma once
// ==================================================================
//  ZLG USBCAN 标志位解析器(对应 .NET Comm/ZlgCanFlagParser.cs)
//  帧类型信息编码在 can_id 高位(SocketCAN 风格);
//  CAN FD 的 BRS/ESI 在 canfd_frame.flags:bit0 BRS, bit1 ESI。
// ==================================================================
#include <cstdint>
#include <memory>

#include <canhw/CanFrameHelper.h>
#include <canhw/Frame.h>

namespace canhw {
namespace zlg {

class ZlgFlagParser {
public:
    /// <param name="rawCanId">原始 can_id(未剥离高位标志)</param>
    /// <param name="fdFlags">CAN FD 帧的 flags 字节,经典 CAN 传 0</param>
    /// <param name="isFd">是否 CAN FD 通道</param>
    static std::shared_ptr<const CanFrameFlags> Parse(uint32_t rawCanId, uint8_t fdFlags,
                                                      bool isFd) {
        constexpr uint8_t CANFD_BRS = 0x01;
        constexpr uint8_t CANFD_ESI = 0x02;

        auto f = std::make_shared<CanFrameFlags>();
        const bool isExt = (rawCanId & CanFrameDef::CAN_EFF_FLAG) != 0;

        f->rawFlags = rawCanId;
        f->isRTR = (rawCanId & CanFrameDef::CAN_RTR_FLAG) != 0;
        f->isStandardFrame = !isExt;
        f->isExtendedFrame = isExt;
        f->isErrorFrame = (rawCanId & CanFrameDef::CAN_ERR_FLAG) != 0;
        f->isCanFd = isFd;
        f->hasBrs = isFd && (fdFlags & CANFD_BRS) != 0;
        f->hasEsi = isFd && (fdFlags & CANFD_ESI) != 0;

        return f;
    }
};

} // namespace zlg
} // namespace canhw
