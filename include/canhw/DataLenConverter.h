#pragma once
#include <array>
#include <stdexcept>
#include <string>

namespace canhw {

/// CAN/CANFD 数据长度校验与 DLC 编码换算(对应 .NET DataLenConverter)
class DataLenConverter {
public:
    static constexpr int MaxCanDataLength = 8;
    static constexpr int MaxCanFdDataLength = 64;

    /// 经典 CAN:0-8 任意长度
    static bool IsValidCanLength(int length) {
        return length >= 0 && length <= MaxCanDataLength;
    }

    /// CAN FD:仅允许 0-8,12,16,20,24,32,48,64
    static bool IsValidCanFdLength(int length) {
        for (int v : kCanFdLengths) {
            if (v == length) return true;
        }
        return false;
    }

    /// 实际数据长度 -> DLC 编码(0-15),非法长度抛异常
    static int LengthToDlc(int length) {
        if (length <= MaxCanDataLength) return length < 0 ? 0 : length;
        for (int i = 0; i < static_cast<int>(kCanFdLengths.size()); ++i) {
            if (kCanFdLengths[i] == length) return i;
        }
        throw std::invalid_argument(
            "Invalid CANFD data length(" + std::to_string(length) +
            "), valid lengths: 0-8,12,16,20,24,32,48,64.");
    }

    /// DLC 编码(0-15) -> 实际数据长度,越界钳制到 0/64
    static int DlcToLength(int dlc) {
        if (dlc < 0) return 0;
        if (dlc >= static_cast<int>(kCanFdLengths.size())) return MaxCanFdDataLength;
        return kCanFdLengths[dlc];
    }

private:
    /// CAN FD 数据段允许的离散长度
    static constexpr std::array<int, 16> kCanFdLengths = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
};

} // namespace canhw
