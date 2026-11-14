#pragma once
// ==================================================================
//  设备/通道枚举(供 GUI「刷新设备」使用)
//  借鉴 QC2015P_Cpp_Plus 的 ICanDevice::listChannels + CanChannelInfo:
//  面板刷新设备时枚举厂商通道的名称与可用性,且尽量零副作用。
//  其他厂商的枚举按同一 ChannelInfo 约定逐步补充。
// ==================================================================
#include <vector>

#include "ChannelInfo.h"

namespace canhw {

/// 枚举 PEAK PCAN_USBBUS1..16 通道(名称/可用性)。
/// 依据 PCAN_CHANNEL_CONDITION 探测,不打开/不初始化通道,零副作用。
std::vector<ChannelInfo> ListPeakChannels();

} // namespace canhw
