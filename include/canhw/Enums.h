#pragma once
#include <cstdint>

namespace canhw {

// ==================================================================
//  硬件厂商 / 通信协议(对应 .NET HardwareEnums.cs)
// ==================================================================

/// 硬件厂商
enum class HardwareManufacturer : uint8_t {
    Kvaser,
    Peak,
    Tosun,
    Zlg,
    Vector,
    Esd,
    Ni,
    Etas,
    Intrepid,
    EbEl,
    ChuangXin,
    Toomoss,
};

/// 总线通信协议
enum class CommProtocol : uint8_t {
    CAN,
    CANFD,
    LIN,
};

// ==================================================================
//  各厂商设备类型枚举(设备类型必须使用对应厂商自己的枚举)
//  与 .NET 版 EquipmentTypeMap 的强校验语义一致,见 Factory.h
// ==================================================================

/// PEAK 设备类型
enum class PeakDeviceType : uint8_t {
    Generic,
};

/// ZLG(周立功)设备类型
enum class ZlgDeviceType : uint8_t {
    USBCAN,       // 经典 CAN 设备(USBCAN-II / USBCAN-2E-U 等,ControlCAN 系)
    USBCANFD200U, // USBCANFD-200U 等,同时支持 CAN 与 CANFD
};

/// Kvaser 设备类型
enum class KvaserDeviceType : uint8_t {
    Generic,
};

/// 同星 Tosun 设备类型
enum class TosunDeviceType : uint8_t {
    Generic,
};

/// Vector 设备类型
enum class VectorDeviceType : uint8_t {
    Generic,
};

/// ESD(esd electronics)设备类型
enum class EsdDeviceType : uint8_t {
    Generic,
};

/// NI(National Instruments)设备类型
enum class NiDeviceType : uint8_t {
    Generic,
};

/// ETAS 设备类型(BOA V2 驱动,不按型号区分)
enum class EtasDeviceType : uint8_t {
    Generic,
};

/// Intrepid 英特佩斯设备类型(icsneo40 API,不按型号区分)
enum class IntrepidDeviceType : uint8_t {
    Generic,
};

/// Eberspächer EbEl 设备类型(FlexCard,仅经典 CAN,无 CAN FD)
enum class EbElDeviceType : uint8_t {
    Generic,
};

/// 创芯科技设备类型:
/// USBCAN  -> ControlCAN.dll(VCI_ API),USBCAN-2A/2C、CANalyst-II 等;
/// USBCANFD-> ControlCANFD.dll(ZCAN_ API),USBCANFD 分析仪(200U/100U/MINI)。
enum class ChuangXinDeviceType : uint8_t {
    USBCAN,
    USBCANFD,
};

/// 图莫斯 Toomoss USB2XXX 设备类型(CAN/CANFD 为同一设备的不同协议通路)
enum class ToomossDeviceType : uint8_t {
    Generic,
};

// ==================================================================
//  ZLG 专属枚举
// ==================================================================

/// ZLG 设备型号,枚举值即 ZCAN_OpenDevice 的 device_type 编号
enum class ZlgDeviceModel : uint32_t {
    // ZCAN_USBCAN1      = 3,
    USBCAN_II    = 4,
    // ZCAN_USBCAN_E_U   = 20,
    USBCAN_2E_U  = 21,
    // ZCAN_PCIECANFD_100U = 38,
    // ZCAN_PCIECANFD_200U = 39,
    USBCANFD_200U = 41,
    // ZCAN_USBCANFD_100U = 42,
    // ZCAN_USBCANFD_MINI  = 43,
};

/// ZLG 经典 CAN 波特率
enum class ZlgCanBitrate : uint8_t {
    _50K, _100K, _125K, _250K, _500K, _1M,
};

/// ZLG CAN FD 数据段波特率(带采样点标识)
enum class ZlgCanFDBitrate : uint8_t {
    _500K_80P, _1M_80P, _2M_80P, _2M_60P, _4M_80P, _8M_60P, _8M_80P, _8M_70P,
};

/// ZLG CAN FD 标准类型(0=ISO,1=BOSCH 非 ISO)
enum class ZlgCANFDStandard : uint8_t {
    CANFD_ISO,
    CANFD_BOSCH,
};

// ==================================================================
//  PEAK 专属枚举
// ==================================================================

/// PEAK CAN 波特率,枚举值即 PCAN-Basic 的 BTR0BTR1 编码
enum class PeakCanBitrate : uint16_t {
    pcanBAUD_50K  = 0x472F,
    pcanBAUD_100K = 0x432F,
    pcanBAUD_125K = 0x031C,
    pcanBAUD_250K = 0x011C,
    pcanBAUD_500K = 0x001C,
    pcanBAUD_1M   = 0x0014,
};

// ==================================================================
//  字符串辅助(用于日志 / 打印硬件清单)
// ==================================================================

inline const char* ToString(HardwareManufacturer m) {
    switch (m) {
        case HardwareManufacturer::Kvaser:    return "Kvaser";
        case HardwareManufacturer::Peak:      return "Peak";
        case HardwareManufacturer::Tosun:     return "Tosun";
        case HardwareManufacturer::Zlg:       return "Zlg";
        case HardwareManufacturer::Vector:    return "Vector";
        case HardwareManufacturer::Esd:       return "Esd";
        case HardwareManufacturer::Ni:        return "Ni";
        case HardwareManufacturer::Etas:      return "Etas";
        case HardwareManufacturer::Intrepid:  return "Intrepid";
        case HardwareManufacturer::EbEl:      return "EbEl";
        case HardwareManufacturer::ChuangXin: return "ChuangXin";
        case HardwareManufacturer::Toomoss:   return "Toomoss";
    }
    return "Unknown";
}

inline const char* ToString(CommProtocol p) {
    switch (p) {
        case CommProtocol::CAN:   return "CAN";
        case CommProtocol::CANFD: return "CANFD";
        case CommProtocol::LIN:   return "LIN";
    }
    return "Unknown";
}

/// 设备类型枚举值的名字(按厂商解释 int 值,用于打印硬件清单)
inline const char* EquipmentTypeName(HardwareManufacturer m, int equipmentType) {
    switch (m) {
        case HardwareManufacturer::Zlg:
            switch (equipmentType) {
                case 0:  return "USBCAN";
                case 1:  return "USBCANFD200U";
            }
            break;
        case HardwareManufacturer::ChuangXin:
            switch (equipmentType) {
                case 0:  return "USBCAN";
                case 1:  return "USBCANFD";
            }
            break;
        case HardwareManufacturer::Peak:
        case HardwareManufacturer::Kvaser:
        case HardwareManufacturer::Tosun:
        case HardwareManufacturer::Vector:
        case HardwareManufacturer::Esd:
        case HardwareManufacturer::Ni:
        case HardwareManufacturer::Etas:
        case HardwareManufacturer::Intrepid:
        case HardwareManufacturer::EbEl:
        case HardwareManufacturer::Toomoss:
            if (equipmentType == 0) return "Generic";
            break;
        default:
            break;
    }
    return "?";
}

} // namespace canhw
