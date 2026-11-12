#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- 图莫斯专属枚举(对应 .NET Comm/ToomossCanMode.cs) ----------------

/// 经典 CAN 工作模式(CAN_INIT_CONFIG.CAN_Mode 低 7 位)
enum class ToomossCanMode : uint8_t {
    Normal = 0,
    LoopBack = 1,
    Silent = 2,
    SilentLoopBack = 3,
};

/// CANFD 工作模式(CANFD_INIT_CONFIG.Mode)
enum class ToomossCanFdMode : uint8_t {
    Normal = 0,
    SelfReceive = 1,
};

/// 经典 CAN 过滤器模式(CAN_FILTER_CONFIG.FilterMode)
enum class ToomossCanFilterMode : uint8_t {
    Mask = 0,
    List = 1,
};

// ---------------- 配置(对应 .NET Parameters/Toomoss*.cs) ----------------

/// 图莫斯 USB2XXX 经典 CAN 配置
struct ToomossCANConfig : BusConfigBase {
    uint32_t deviceIndex = 0;   ///< 设备索引号(USB_ScanDevice 返回的设备序号)
    uint8_t channelIndex = 0;   ///< CAN 通道号(0 基)
    uint32_t canBitrate = 500000; ///< CAN 波特率(bps,由 CAN_GetCANSpeedArg 自动计算时序)
    ToomossCanMode mode = ToomossCanMode::Normal;
    bool enableA120Ohm = false;       ///< 启用内部 120 欧姆终端电阻
    bool enableAutoOffline = false;   ///< 启用自动离线管理(CAN_ABOM)
    bool disableRetransmission = false; ///< 禁止报文自动重传(CAN_NART=1)
    bool fifoLock = false;            ///< FIFO 锁定(CAN_RFLM=1)
    bool txPriorityByRequest = false; ///< 请求顺序优先级(CAN_TXFP=1)
    bool enableFilter = false;        ///< 启用硬件过滤器
    uint8_t filterIndex = 0;          ///< 过滤器索引(0~13)
    ToomossCanFilterMode filterMode = ToomossCanFilterMode::Mask;
    bool extendedFilter = false;      ///< 过滤器使用扩展帧
    uint32_t filterId = 0;            ///< 验收码 ID
    uint32_t filterRtr = 0;           ///< 验收码 RTR 位
    uint32_t filterMask = 0xFFFFFFFF; ///< 屏蔽码(对应位为 1 则需要比较)
    uint32_t filterMaskIde = 0;
    uint32_t filterMaskRtr = 0;
};

/// 图莫斯 USB2XXX CANFD 配置(同一通道可收发经典帧,由 FDF 区分)
struct ToomossCanFDConfig : BusConfigBase {
    uint32_t deviceIndex = 0;
    uint8_t channelIndex = 0;
    uint32_t arbitrationBitrate = 500000; ///< 仲裁段波特率(bps)
    uint32_t dataBitrate = 2000000;       ///< 数据段波特率(bps)
    ToomossCanFdMode mode = ToomossCanFdMode::Normal;
    bool enableISOCRC = true;   ///< 启用 ISO CRC
    bool enableRetrySend = true;///< 启用报文自动重传
    bool enableA120Ohm = false; ///< 启用内部 120 欧姆终端电阻
    bool enableFilter = false;  ///< 启用 CANFD 硬件过滤器(不启用时默认接收所有帧)
    uint8_t filterIndex = 0;    ///< 过滤器索引(0~31)
    bool extendedFilter = false;///< 过滤器使用扩展帧
    uint32_t filterId = 0;      ///< 验收码 ID(bit28..0)
    uint32_t filterMask = 0x1FFFFFFF; ///< 屏蔽码
};

} // namespace canhw
