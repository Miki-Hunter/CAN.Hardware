#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- Kvaser 专属枚举(对应 .NET Comm/KvaserCanBitrate 等) ----------------

/// Kvaser 经典 CAN 波特率(值即 canlib 预设码,见 KvaserApi::BuildCanBitrate)
enum class KvaserCanBitrate : uint8_t {
    canBITRATE_10K, canBITRATE_50K, canBITRATE_62K, canBITRATE_83K,
    canBITRATE_100K, canBITRATE_125K, canBITRATE_250K, canBITRATE_500K,
    canBITRATE_1M,
};

/// Kvaser CAN FD 数据段波特率(值即 canlib FD 预设码)
enum class KvaserCanFdBitrate : uint8_t {
    canFD_BITRATE_500K_80P, canFD_BITRATE_1M_80P, canFD_BITRATE_2M_80P,
    canFD_BITRATE_4M_80P, canFD_BITRATE_8M_60P,
};

// ---------------- 配置(对应 .NET Parameters/KvaserCANConfig 等) ----------------

/// Kvaser 经典 CAN 配置
struct KvaserCANConfig : BusConfigBase {
    uint32_t canDeviceChannel = 0;                 ///< CAN 设备通道号
    KvaserCanBitrate canBitrate = KvaserCanBitrate::canBITRATE_500K;
    // 自定义 TQ 位时序(EnableCustomBitrate=true 时生效)
    int tq = 8;          ///< 总时间量子数
    int phase1 = 2;      ///< 相位段1
    int phase2 = 2;      ///< 相位段2
    int sjw = 1;         ///< 同步跳转宽度
    int prop = 0;        ///< 传播时间段
    int prescale = 1;    ///< 预分频值
    bool enableCustomBitrate = false;
    bool enableFilter = false;                    ///< 启用硬件过滤器
    uint32_t filterCode = 0;                      ///< 过滤器代码值
    uint32_t filterMask = 0;                      ///< 过滤器掩码值
    bool filterIsExtended = false;                ///< 过滤器是否为扩展帧(.NET 字段名 Fileter_IsExtended)
};

/// Kvaser CAN FD 配置
struct KvaserCanFDConfig : BusConfigBase {
    uint32_t canFdDeviceChannel = 0;              ///< CAN FD 设备通道号
    KvaserCanBitrate canFdArbitrationBitrate = KvaserCanBitrate::canBITRATE_500K;
    KvaserCanFdBitrate canFdDataBitrate = KvaserCanFdBitrate::canFD_BITRATE_2M_80P;
    // 仲裁段自定义 TQ 位时序
    int arbitrationTq = 40;
    int arbitrationPhase1 = 8;
    int arbitrationPhase2 = 8;
    int arbitrationSjw = 8;
    int arbitrationProp = 23;
    int arbitrationPrescale = 4;
    // 数据段自定义 TQ 位时序
    int dataTq = 20;
    int dataPhase1 = 8;
    int dataPhase2 = 4;
    int dataSjw = 4;
    int dataProp = 7;
    int dataPrescale = 2;
    bool enableCustomBitrate = false;
    bool enableFilter = false;
    uint32_t filterCode = 0;
    uint32_t filterMask = 0;
    bool filterIsExtended = false;
};

} // namespace canhw
