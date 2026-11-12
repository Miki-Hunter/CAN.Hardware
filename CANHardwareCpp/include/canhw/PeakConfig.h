#pragma once
#include <cstdint>

#include "Config.h"
#include "Enums.h"

namespace canhw {

/// PEAK 经典 CAN 配置(对应 .NET PeakCANConfig)
struct PeakCANConfig : BusConfigBase {
    /// CAN 设备通道句柄(如 0x51=PCAN_USBBUS1,PCAN 通道句柄非 0 基连续)
    uint16_t canDeviceChannel = 0x51;
    PeakCanBitrate canBitrate = PeakCanBitrate::pcanBAUD_500K; ///< CAN 波特率(BTR0BTR1)
    bool enableFilter = false;         ///< 启用硬件过滤器
    /// PEAK 过滤器使用 ID 区间(FromID~ToID),而非掩码方式
    uint32_t filterFromId = 0;         ///< 过滤器起始 ID
    uint32_t filterToId = 0;           ///< 过滤器结束 ID
    uint32_t ioPort = 0;               ///< I/O 端口地址(仅非即插即用设备使用)
    uint16_t interrupt = 0;            ///< 中断号(仅非即插即用设备使用)
};

/// PEAK CAN FD 配置(对应 .NET PeakCanFDConfig)
/// FD 波特率由位时序参数(f_clock/brp/tseg1/tseg2/sjw)拼成 InitializeFD 要求的字符串
struct PeakCanFDConfig : BusConfigBase {
    uint16_t canFdDeviceChannel = 0x51; ///< CAN FD 设备通道句柄(如 0x51=PCAN_USBBUS1)
    uint32_t clock = 80000000;          ///< 控制器时钟频率(Hz)
    // 仲裁段位时序
    uint32_t arbitrationBrp = 10;
    uint32_t arbitrationTseg1 = 12;
    uint32_t arbitrationTseg2 = 3;
    uint32_t arbitrationSjw = 1;
    // 数据段位时序
    uint32_t dataBrp = 4;
    uint32_t dataTseg1 = 7;
    uint32_t dataTseg2 = 2;
    uint32_t dataSjw = 1;
    bool enableFilter = false;
    uint32_t filterFromId = 0;
    uint32_t filterToId = 0;
};

} // namespace canhw
