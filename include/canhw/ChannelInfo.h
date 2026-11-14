#pragma once
#include <cstdint>
#include <string>

namespace canhw {

/// 通道清单条目(供 GUI「刷新设备」展示)。
/// 借鉴 QC2015P_Cpp_Plus 的 CanChannelInfo(其 ICanDevice::listChannels)。
struct ChannelInfo {
    uint32_t channel = 0;    ///< 厂商通道号(PEAK 为句柄 0x51…;ZLG 等为 0 基通道)
    bool available = false;  ///< 当前是否可用(未被占用/可打开)
    std::string name;        ///< 通道名(驱动版本串或逻辑名)
};

} // namespace canhw
