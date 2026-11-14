// ==================================================================
//  PEAK 通道枚举(供 GUI「刷新设备」)
//  借鉴 QC2015P_Cpp_Plus:PCAN 用 PCAN_CHANNEL_CONDITION 探测句柄存在性,
//  句柄 = 0x51(PCAN_USBBUS1)起连续编号;另读出 PCAN_CHANNEL_VERSION
//  作为通道名。整个过程不打开/不初始化通道,零副作用。
// ==================================================================
#include <canhw/Discovery.h>

#include <string>

#include "PeakApi.h"

namespace canhw {

std::vector<ChannelInfo> ListPeakChannels() {
    std::vector<ChannelInfo> channels;
    auto& api = peak::PeakApi::Instance();

    // PCAN_USBBUS1..16(0x51..0x60)
    for (uint16_t handle = 0x51; handle <= 0x60; ++handle) {
        uint32_t condition = 0;
        const uint32_t status =
            api.GetValue(handle, peak::PCAN_CHANNEL_CONDITION, &condition,
                         sizeof(condition));
        // 不存在的句柄返回错误;明确 UNAVAILABLE 的也跳过
        if (status != peak::PCAN_ERROR_OK) continue;
        if (condition == peak::PCAN_CHANNEL_UNAVAILABLE) continue;

        ChannelInfo info;
        info.channel = handle;
        info.available = (condition & peak::PCAN_CHANNEL_AVAILABLE) != 0;
        info.name = "PCAN_USBBUS" + std::to_string(handle - 0x50);

        // 读通道/硬件版本串作为更友好的名称(best-effort)。
        // 该串是多行驱动横幅,只取第一行作为名称
        char version[256] = {};
        if (api.GetValue(handle, peak::PCAN_CHANNEL_VERSION, version,
                         sizeof(version)) == peak::PCAN_ERROR_OK &&
            version[0] != '\0') {
            std::string firstLine(version);
            const size_t cut = firstLine.find_first_of("\r\n");
            if (cut != std::string::npos) firstLine.resize(cut);
            if (!firstLine.empty()) info.name = firstLine;
        }
        channels.push_back(info);
    }
    return channels;
}

} // namespace canhw
