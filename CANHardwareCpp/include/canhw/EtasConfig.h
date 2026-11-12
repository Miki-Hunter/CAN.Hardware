#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- ETAS 专属枚举(对应 .NET Comm/EtasCanBitrate.cs) ----------------

/// ETAS BOA CAN 波特率(值即 Hz,直接填入 OCI_CANConfiguration.baudrate)
enum class EtasCanBitrate : uint32_t {
    etasBAUD_125K = 125000,
    etasBAUD_250K = 250000,
    etasBAUD_500K = 500000,
    etasBAUD_1M = 1000000,
};

/// ETAS BOA CAN FD 数据段波特率(值即 Hz)
enum class EtasCanFdBitrate : uint32_t {
    etasFDBAUD_500K = 500000,
    etasFDBAUD_1M = 1000000,
    etasFDBAUD_2M = 2000000,
    etasFDBAUD_4M = 4000000,
    etasFDBAUD_5M = 5000000,
    etasFDBAUD_8M = 8000000,
};

// ---------------- 配置(对应 .NET Parameters/Etas*.cs) ----------------

/// ETAS 经典 CAN 配置
struct EtasCANConfig : BusConfigBase {
    int32_t channelIndex = 0;  ///< CAN 通道索引(0 基,按 CSI 枚举排序),仅 interfaceUri 为空时生效
    std::string interfaceUri;  ///< BOA 硬件 URI(如 /ETAS.ETH://ES581.4/CAN:1),非空时跳过 CSI 枚举
    EtasCanBitrate canBitrate = EtasCanBitrate::etasBAUD_500K;
    uint32_t customBitrate = 500000;  ///< 自定义波特率(Hz)
    uint32_t samplePoint = 75;        ///< 采样点(单位 1%,75=75%)
    uint32_t btlCycles = 10;          ///< 每比特时间量子数
    uint32_t sjw = 1;                 ///< 同步跳转宽度
    bool enableCustomBitrate = false;
};

/// ETAS CAN FD 配置
struct EtasCanFDConfig : BusConfigBase {
    int32_t channelIndex = 0;
    std::string interfaceUri;
    EtasCanBitrate canFdArbitrationBitrate = EtasCanBitrate::etasBAUD_500K;
    EtasCanFdBitrate canFdDataBitrate = EtasCanFdBitrate::etasFDBAUD_2M;
    // 仲裁段自定义
    uint32_t customArbitrationBitrate = 500000;
    uint32_t arbitrationSamplePoint = 75;
    uint32_t arbitrationBtlCycles = 10;
    uint32_t arbitrationSjw = 1;
    // 数据段自定义
    uint32_t customDataBitrate = 2000000;
    uint32_t dataSamplePoint = 80;
    uint32_t dataBtlCycles = 10;
    uint32_t dataSjw = 1;
    bool enableCustomBitrate = false;
};

} // namespace canhw
