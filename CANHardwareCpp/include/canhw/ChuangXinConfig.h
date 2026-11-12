#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

// ---------------- 创芯专属枚举(对应 .NET Comm/ChuangXin*.cs) ----------------

/// 创芯经典 CAN 波特率(ControlCAN 用 Timing0/Timing1 寄存器值,对应关系见 ChuangXinCanApi::BuildTiming)
enum class ChuangXinCanBitrate : uint8_t {
    _50K, _100K, _125K, _250K, _500K, _800K, _1M,
};

/// 创芯 USBCANFD 数据段波特率(ZCAN 直接用 bps 数值)
enum class ChuangXinCanFdBitrate : uint8_t {
    _100K, _125K, _250K, _500K, _800K, _1M, _2M, _4M, _5M,
};

/// 创芯经典 CAN 设备型号,值即 ControlCAN(VCI)的 DeviceType
enum class ChuangXinCanDeviceModel : uint32_t {
    USBCAN1 = 3,      ///< 兼容 ZLG USBCAN1(单通道)
    USBCAN2 = 4,      ///< 双通道经典 CAN(USBCAN-2A/2C、CANalyst-II 系列默认)
    USBCAN_E_U = 20,  ///< 兼容 ZLG USBCAN-E-U
    USBCAN_2E_U = 21, ///< 兼容 ZLG USBCAN-2E-U
};

/// 创芯 USBCANFD 设备型号,值即 ControlCANFD(ZCAN)的 device_type
enum class ChuangXinCanFdDeviceModel : uint32_t {
    USBCANFD_200U = 41, ///< 2 通道
    USBCANFD_100U = 42, ///< 1 通道
    USBCANFD_MINI = 43, ///< 1 通道
};

/// 创芯经典 CAN 工作模式(VCI_INIT_CONFIG.Mode)
enum class ChuangXinCanMode : uint8_t {
    Normal = 0,
    ListenOnly = 1,
    SelfTest = 2,
};

/// 创芯 USBCANFD 工作模式(canfd.mode)
enum class ChuangXinCanFdMode : uint8_t {
    Normal = 0,
    ListenOnly = 1,
};

/// 创芯 USBCANFD 的 CAN FD 标准(0=ISO,1=BOSCH 非 ISO)
enum class ChuangXinCANFDStandard : uint8_t {
    CANFD_ISO,
    CANFD_BOSCH,
};

// ---------------- 配置(对应 .NET Parameters/ChuangXin*.cs) ----------------

/// 创芯经典 CAN 分析仪配置(USBCAN-2A/2C、CANalyst-II 系列,ControlCAN.dll)
struct ChuangXinCANConfig : BusConfigBase {
    ChuangXinCanDeviceModel deviceModel = ChuangXinCanDeviceModel::USBCAN2;
    uint32_t deviceIndex = 0;   ///< 设备索引号
    uint32_t deviceChannel = 0; ///< 设备通道号(0 基)
    ChuangXinCanBitrate canBitrate = ChuangXinCanBitrate::_500K;
    bool enableCustomBitrate = false; ///< 启用自定义 Timing0/Timing1
    uint8_t timing0 = 0x00;           ///< 自定义波特率 Timing0 寄存器值
    uint8_t timing1 = 0x1C;           ///< 自定义波特率 Timing1 寄存器值
    ChuangXinCanMode mode = ChuangXinCanMode::Normal;
    bool enableFilter = false;        ///< 启用硬件过滤器
    bool extendedFilter = false;      ///< 过滤器使用扩展帧
    uint32_t accCode = 0;             ///< 验收码
    uint32_t accMask = 0xFFFFFFFF;    ///< 验收掩码
};

/// 创芯 USBCANFD 分析仪配置(200U/100U/MINI,ControlCANFD.dll)
struct ChuangXinCanFDConfig : BusConfigBase {
    ChuangXinCanFdDeviceModel deviceModel = ChuangXinCanFdDeviceModel::USBCANFD_200U;
    uint32_t deviceIndex = 0;
    uint32_t deviceChannel = 0;
    ChuangXinCANFDStandard canfdStandard = ChuangXinCANFDStandard::CANFD_ISO;
    /// false=仅经典 CAN(通道仍按 CANFD 初始化)
    bool useCanFD = true;
    ChuangXinCanBitrate arbitrationBitrate = ChuangXinCanBitrate::_500K;
    ChuangXinCanFdBitrate dataBitrate = ChuangXinCanFdBitrate::_2M;
    bool enableCustomBitrate = false;
    /// 自定义波特率内容(由厂商波特率计算工具生成)
    std::string customBitrateContent;
    ChuangXinCanFdMode mode = ChuangXinCanFdMode::Normal;
    bool enableA120Ohm = false;   ///< 启用 120 欧姆终端电阻
    bool enableFilter = false;
    bool extendedFilter = false;  ///< 过滤器使用扩展帧
    uint32_t filterFromId = 0;    ///< 过滤器起始 ID
    uint32_t filterToId = 0;      ///< 过滤器结束 ID
};

} // namespace canhw
