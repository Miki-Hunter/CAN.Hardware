#pragma once
#include <cstdint>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- Intrepid 专属枚举(对应 .NET Comm/IntrepidCanBitrate.cs) ----------------

/// Intrepid CAN 波特率(值即 bps,直接传给 icsneoSetBitRate)
enum class IntrepidCanBitrate : int32_t {
    icsBAUD_10K = 10000,
    icsBAUD_20K = 20000,
    icsBAUD_50K = 50000,
    icsBAUD_100K = 100000,
    icsBAUD_125K = 125000,
    icsBAUD_250K = 250000,
    icsBAUD_500K = 500000,
    icsBAUD_800K = 800000,
    icsBAUD_1M = 1000000,
};

/// Intrepid CAN FD 数据段波特率(值即 bps,直接传给 icsneoSetFDBitRate)
enum class IntrepidCanFdBitrate : int32_t {
    icsFDBAUD_500K = 500000,
    icsFDBAUD_1M = 1000000,
    icsFDBAUD_2M = 2000000,
    icsFDBAUD_4M = 4000000,
    icsFDBAUD_5M = 5000000,
    icsFDBAUD_8M = 8000000,
    icsFDBAUD_10M = 10000000,
};

// ---------------- 配置(对应 .NET Parameters/Intrepid*.cs) ----------------

/// Intrepid 经典 CAN 配置
struct IntrepidCANConfig : BusConfigBase {
    uint32_t channel = 0;  ///< CAN 通道号(0 基,0~6 对应设备 HSCAN1~HSCAN7)
    IntrepidCanBitrate canBitrate = IntrepidCanBitrate::icsBAUD_500K;
    bool enableCustomBitrate = false;
    int32_t customBitrate = 500000;  ///< 自定义波特率 bps(驱动自动取最近值)
};

/// Intrepid CAN FD 配置
struct IntrepidCanFDConfig : BusConfigBase {
    uint32_t channel = 0;  ///< CAN FD 通道号(0 基,0~6 对应 HSCAN1~HSCAN7)
    IntrepidCanBitrate canFdArbitrationBitrate = IntrepidCanBitrate::icsBAUD_500K;
    IntrepidCanFdBitrate canFdDataBitrate = IntrepidCanFdBitrate::icsFDBAUD_2M;
    bool enableCustomBitrate = false;
    int32_t customArbitrationBitrate = 500000;  ///< 自定义仲裁段波特率 bps
    int32_t customDataBitrate = 2000000;        ///< 自定义数据段波特率 bps
};

} // namespace canhw
