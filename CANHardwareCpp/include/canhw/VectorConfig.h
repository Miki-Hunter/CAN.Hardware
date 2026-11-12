#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- Vector 专属枚举(对应 .NET BaseLibDll/VectorCanLibDLL.cs) ----------------

/// Vector 硬件类型(XL HWTYPE_xxx,参考 vxlapi.h)
enum class VectorHwType : uint32_t {
    NONE = 0,
    Virtual = 1,
    CANcardX = 2,
    CANAC2PCI = 6,
    CANcardY = 12,
    CANcardXL = 15,
    CANcaseXL = 21,
    CANBOARDXL = 25,
    CANBOARDXL_PXI = 27,
    VN2600 = 29,
    VN2610 = 29,
    VN3300 = 37,
    VN3600 = 39,
    VN7600 = 41,
    CANcardXLe = 43,
    VN8900 = 45,
    VN8950 = 47,
    VN2640 = 53,
    VN1610 = 55,
    VN1630 = 57,
    VN1640 = 59,
    VN8970 = 61,
    VN1611 = 63,
    VN5240 = 64,
    VN5610 = 65,
    VN5620 = 66,
    VN7570 = 67,
    VN5650 = 68,
    IPCLIENT = 69,
    VX1121 = 73,
    VX1131 = 75,
    VT6204 = 77,
    VN1630_LOG = 79,
    VN7610 = 81,
    VN7572 = 83,
    VN8972 = 85,
    VN0601 = 87,
    VN5640 = 89,
    VX0312 = 91,
    VN8800 = 95,
    IPCL8800 = 96,
    IPSRV8800 = 97,
    CSMCAN = 98,
    VN5610A = 101,
    VN7640 = 102,
    VX1135 = 104,
    VN4610 = 105,
    VT6306 = 107,
    VT6104A = 108,
    VN5430 = 109,
    VTSSERVICE = 110,
    VN1530 = 112,
    VN1531 = 113,
    VX1161A = 114,
    VX1161B = 115,
};

/// Vector CAN 标准波特率(值即实际波特率 bps,直接传入 xlCanSetChannelBitrate)
enum class VectorCanBitrate : uint32_t {
    canBITRATE_10K = 10000,
    canBITRATE_20K = 20000,
    canBITRATE_50K = 50000,
    canBITRATE_100K = 100000,
    canBITRATE_125K = 125000,
    canBITRATE_250K = 250000,
    canBITRATE_500K = 500000,
    canBITRATE_800K = 800000,
    canBITRATE_1M = 1000000,
};

/// Vector CAN FD 数据段波特率(值即实际波特率 bps)
enum class VectorCanFdBitrate : uint32_t {
    canFD_BITRATE_500K = 500000,
    canFD_BITRATE_1M = 1000000,
    canFD_BITRATE_2M = 2000000,
    canFD_BITRATE_4M = 0 + 4000000,
    canFD_BITRATE_5M = 5000000,
    canFD_BITRATE_8M = 8000000,
};

// ---------------- 配置(对应 .NET Parameters/VectorCan*.cs) ----------------
// 说明:.NET 的 Channel 为 XLchannelInfo(WPF 下拉选择对象),纯逻辑层
// 只用到其 HwChannel,这里简化为 hardwareType + hwChannel 两个字段;
// AvailableChannels 下拉辅助(GUI 专用)未移植。

/// Vector 经典 CAN 配置
struct VectorCanConfig : BusConfigBase {
    VectorHwType hardwareType = VectorHwType::VN1630; ///< Vector 硬件类型
    uint32_t hwChannel = 0;                            ///< 硬件通道号(驱动内同型号设备的序号)
    VectorCanBitrate canBitrate = VectorCanBitrate::canBITRATE_500K;
    uint32_t customBitrate = 500000;  ///< 自定义波特率(bps)
    int tseg1 = 13;                   ///< 时间段1
    int tseg2 = 2;                    ///< 时间段2
    int sjw = 1;                      ///< 同步跳转宽度
    int sam = 1;                      ///< 采样次数(1或3)
    bool enableCustomBitrate = false;
    bool enableFilter = false;        ///< 启用硬件过滤器
    uint32_t filterCode = 0;          ///< 过滤器代码值(Vector 用 Code/Mask 掩码)
    uint32_t filterMask = 0;          ///< 过滤器掩码值
    bool filterIsExtended = false;    ///< 过滤器是否为扩展帧
};

/// Vector CAN FD 配置
struct VectorCanFDConfig : BusConfigBase {
    VectorHwType hardwareType = VectorHwType::VN1630;
    uint32_t hwChannel = 0;
    VectorCanBitrate canFdArbitrationBitrate = VectorCanBitrate::canBITRATE_500K;
    VectorCanFdBitrate canFdDataBitrate = VectorCanFdBitrate::canFD_BITRATE_2M;
    // 仲裁段自定义时序
    uint32_t customArbitrationBitrate = 500000;
    int arbitrationTseg1 = 119;
    int arbitrationTseg2 = 40;
    int arbitrationSjw = 40;
    // 数据段自定义时序
    uint32_t customDataBitrate = 2000000;
    int dataTseg1 = 29;
    int dataTseg2 = 10;
    int dataSjw = 10;
    bool enableNonIso = false;        ///< 启用 Non-ISO(CANFD-BOSCH)
    bool enableCustomBitrate = false;
    bool enableFilter = false;
    uint32_t filterCode = 0;
    uint32_t filterMask = 0;
    bool filterIsExtended = false;
};

} // namespace canhw
