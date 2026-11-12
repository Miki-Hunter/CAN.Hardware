#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- ESD 专属枚举(对应 .NET Comm/EsdCanBitrate.cs) ----------------

/// ESD CAN 标准波特率(值即实际波特率 bps,经 USER_BAUDRATE_NUM|bps 传入 canSetBaudrate)
enum class EsdCanBitrate : uint32_t {
    esdBAUD_10K = 10000,
    esdBAUD_20K = 20000,
    esdBAUD_50K = 50000,
    esdBAUD_100K = 100000,
    esdBAUD_125K = 125000,
    esdBAUD_250K = 250000,
    esdBAUD_500K = 500000,
    esdBAUD_800K = 800000,
    esdBAUD_1M = 1000000,
};

/// ESD CAN FD 数据段波特率(值即实际波特率 bps,经 canSetBaudrateX NUM 模式)
enum class EsdCanFdBitrate : uint32_t {
    esdBAUD_500K = 500000,
    esdBAUD_1M = 1000000,
    esdBAUD_2M = 2000000,
    esdBAUD_4M = 4000000,
    esdBAUD_5M = 5000000,
    esdBAUD_8M = 8000000,
    esdBAUD_10M = 10000000,
};

// ---------------- 配置(对应 .NET Parameters/Esd*.cs) ----------------

/// ESD 经典 CAN 配置
struct EsdCANConfig : BusConfigBase {
    int32_t channel = 0;      ///< CAN 通道号(0 基,直接传给 canOpen)
    EsdCanBitrate canBitrate = EsdCanBitrate::esdBAUD_500K;
    uint32_t customBitrate = 500000;  ///< 自定义波特率(bps,任意值,驱动自动算位时序)
    bool enableCustomBitrate = false;
};

/// ESD CAN FD 配置
struct EsdCanFDConfig : BusConfigBase {
    int32_t channel = 0;      ///< CAN FD 通道号(0 基)
    EsdCanBitrate canFdArbitrationBitrate = EsdCanBitrate::esdBAUD_500K;
    EsdCanFdBitrate canFdDataBitrate = EsdCanFdBitrate::esdBAUD_2M;
    uint32_t customArbitrationBitrate = 500000;  ///< 自定义仲裁段波特率(bps)
    uint32_t customDataBitrate = 2000000;        ///< 自定义数据段波特率(bps)
    bool enableCustomBitrate = false;
};

} // namespace canhw
