#pragma once
#include <cstdint>
#include <string>

#include "Config.h"
#include "Enums.h"

namespace canhw {

/// ZLG 经典 CAN 配置(对应 .NET ZlgCANConfig)
struct ZlgCANConfig : BusConfigBase {
    ZlgDeviceModel deviceModel = ZlgDeviceModel::USBCAN_2E_U; ///< ZLG 设备型号
    uint32_t deviceIndex = 0;          ///< 设备索引号
    uint32_t deviceChannel = 0;        ///< 设备通道号(0 基)
    ZlgCanBitrate canBitrate = ZlgCanBitrate::_500K; ///< CAN 波特率
    bool enableCustomBitrate = false;  ///< 启用自定义波特率
    /// 自定义波特率内容:"波特率(采样点),(Timing0,Timing1)" 格式
    std::string customBitrateContent = "500Kbps(75%),(01,14)";
    bool enableA120Ohm = false;        ///< 启用 120 欧姆终端电阻
    bool enableFilter = false;         ///< 启用硬件过滤器
    /// ZLG 过滤器使用 ID 区间(filter_start~filter_end),而非掩码方式
    uint32_t filterFromId = 0;         ///< 过滤器起始 ID
    uint32_t filterToId = 0;           ///< 过滤器结束 ID
};

/// ZLG CAN FD 配置(对应 .NET ZlgCanFDConfig)
/// USBCANFD-200U 同时支持 CAN 与 CANFD,由 useCanFD 决定工作模式
struct ZlgCanFDConfig : BusConfigBase {
    ZlgDeviceModel deviceModel = ZlgDeviceModel::USBCANFD_200U;
    uint32_t deviceIndex = 0;
    uint32_t deviceChannel = 0;
    ZlgCANFDStandard canfdStandard = ZlgCANFDStandard::CANFD_ISO; ///< CAN FD 标准(ISO/BOSCH)
    bool useCanFD = true;             ///< false = 经典 CAN 模式
    ZlgCanBitrate arbitrationBitrate = ZlgCanBitrate::_500K;  ///< 仲裁段波特率
    ZlgCanFDBitrate dataBitrate = ZlgCanFDBitrate::_2M_80P;  ///< 数据段波特率
    bool enableCustomBitrate = false;
    /// 自定义波特率内容:"仲裁段(采样点),数据段(采样点),(brp,仲裁段时序,数据段时序)" 格式
    std::string customBitrateContent = "500Kbps(80%),2.0Mbps(80%),(80,07C00002,01C00002)";
    bool enableA120Ohm = false;
    bool enableFilter = false;
    uint32_t filterFromId = 0;
    uint32_t filterToId = 0;
};

} // namespace canhw
