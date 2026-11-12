#pragma once
#include <cstdint>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- EbEl 专属枚举(对应 .NET Comm/EbElCanBitrate.cs) ----------------

/// EbEl(Eberspächer Electronics FlexCard)CAN 波特率(值即 bps)。
/// fcbase 无数值波特率接口,Open 时按枚举查位时序表
/// (BaudRatePrescaler/SJW/TS1/TS2,见 EbElCanWrapper::BuildCcConfig)。
enum class EbElCanBitrate : int32_t {
    fcBAUD_10K = 10000,
    fcBAUD_20K = 20000,
    fcBAUD_50K = 50000,
    fcBAUD_100K = 100000,
    fcBAUD_125K = 125000,
    fcBAUD_250K = 250000,
    fcBAUD_500K = 500000,
    fcBAUD_800K = 800000,
    fcBAUD_1M = 1000000,
};

// ---------------- 配置(对应 .NET Parameters/EbElCANConfig.cs) ----------------

/// EbEl 经典 CAN 配置(注意:该 API 只有经典 CAN,无 CAN FD)
struct EbElCANConfig : BusConfigBase {
    uint32_t channel = 0;  ///< CAN 通道号(0 基,0~7 对应设备 CC1~CC8)
    EbElCanBitrate canBitrate = EbElCanBitrate::fcBAUD_500K;
};

} // namespace canhw
