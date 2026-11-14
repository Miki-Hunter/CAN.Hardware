#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- 同星专属枚举(对应 .NET BaseLibDll/ToSunCanLibDLL.cs) ----------------

/// CAN FD 控制器类型
enum class TosunFdControllerType : int32_t {
    lfdtCAN = 0,
    lfdtISOCANFD = 1,
    lfdtNonISOCANFD = 2,
};

/// CAN FD 控制器模式
enum class TosunFdControllerMode : int32_t {
    lfdmNormal = 0,
    lfdmACKOff = 1,
    lfdmRestricted = 2,
};

// ---------------- 配置(对应 .NET Parameters/ToSun*.cs) ----------------

/// 同星经典 CAN 配置
struct ToSunCANConfig : BusConfigBase {
    uint8_t channelIndex = 0;   ///< CAN 通道索引(0 基,CHN1=0)
    double canBitrate = 500;    ///< CAN 波特率(Kbps)
    bool enableA120Ohm = true;  ///< 启用 120 欧姆终端电阻
    bool enableFIFO = true;     ///< 启用 FIFO 模式
    bool enableErrorFrame = true; ///< 启用错误帧接收
    bool enableTurbe = false;   ///< 启用 Turbo 模式(.NET 原名 EnableTurbe)
    bool enableFilter = false;  ///< 启用硬件过滤器
    /// 过滤器 ID 列表(逐 ID 白名单,tsfifo_add_can_canfd_pass_filter)
    std::vector<uint32_t> filterIdArray;
};

/// 同星 CAN FD 配置
struct ToSunCanFDConfig : BusConfigBase {
    uint8_t channelIndex = 0;             ///< CAN FD 通道索引(0 基,CHN1=0)
    double canFdArbitrationBitrate = 500; ///< 仲裁段波特率(Kbps)
    double canFdDataBitrate = 2000;       ///< 数据段波特率(Kbps)
    TosunFdControllerType controllerType = TosunFdControllerType::lfdtISOCANFD;
    TosunFdControllerMode controllerMode = TosunFdControllerMode::lfdmNormal;
    bool enableA120Ohm = true;
    bool enableFIFO = true;
    bool enableErrorFrame = true;
    bool enableTurbe = false;
    /// 启用自定义波特率(寄存器级配置)
    bool enableCustomBitrate = false;
    // 仲裁段位时序
    int arbitrationPhase1 = 8;
    int arbitrationPhase2 = 8;
    int arbitrationSjw = 8;
    int arbitrationPrescale = 1;
    // 数据段位时序
    int dataPhase1 = 8;
    int dataPhase2 = 4;
    int dataSjw = 4;
    int dataPrescale = 1;
    bool enableFilter = false;
    std::vector<uint32_t> filterIdArray;
};

} // namespace canhw
