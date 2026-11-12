#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- NI 专属枚举(对应 .NET Comm/NiCanBitrate.cs) ----------------

/// NI-XNET CAN 波特率(值即 bps,直接写入 Session:Intf:Baud Rate 属性)
enum class NiCanBitrate : uint64_t {
    niBAUD_125K = 125000,
    niBAUD_250K = 250000,
    niBAUD_500K = 500000,
    niBAUD_1M = 1000000,
};

/// NI-XNET CAN FD 数据段波特率(值即 bps)
enum class NiCanFdBitrate : uint64_t {
    niFDBAUD_500K = 500000,
    niFDBAUD_1M = 1000000,
    niFDBAUD_2M = 2000000,
    niFDBAUD_4M = 4000000,
    niFDBAUD_5M = 5000000,
    niFDBAUD_8M = 8000000,
};

// ---------------- 配置(对应 .NET Parameters/Ni*.cs) ----------------

/// NI-XNET 经典 CAN 配置
struct NiCANConfig : BusConfigBase {
    std::string interfaceName = "CAN1";  ///< NI-XNET 接口名(可在 NI MAX 中查看/重命名)
    NiCanBitrate canBitrate = NiCanBitrate::niBAUD_500K;
    bool enableCustomBitrate = false;
    // 自定义位时序默认值:40MHz 时基下 500K、采样点 87.5%
    int tq = 16;         ///< 总时间量子数
    int phase1 = 5;      ///< 相位段1
    int phase2 = 2;      ///< 相位段2
    int sjw = 1;         ///< 同步跳转宽度
    int prop = 8;        ///< 传播时间段
    int prescale = 5;    ///< 预分频值
};

/// NI-XNET CAN FD 配置
struct NiCanFDConfig : BusConfigBase {
    std::string interfaceName = "CAN1";
    NiCanBitrate canFdArbitrationBitrate = NiCanBitrate::niBAUD_500K;
    NiCanFdBitrate canFdDataBitrate = NiCanFdBitrate::niFDBAUD_2M;
    bool enableCustomBitrate = false;
    // 仲裁段位时序(40MHz 时基:500K、采样点 87.5%)
    int arbitrationTq = 16;
    int arbitrationPhase1 = 5;
    int arbitrationPhase2 = 2;
    int arbitrationSjw = 1;
    int arbitrationProp = 8;
    int arbitrationPrescale = 5;
    // 数据段位时序(2M、采样点 80%)
    int dataTq = 20;
    int dataPhase1 = 5;
    int dataPhase2 = 4;
    int dataSjw = 4;
    int dataProp = 10;
    int dataPrescale = 1;
};

} // namespace canhw
