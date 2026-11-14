#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

#include "Enums.h"
#include "IHardware.h"

namespace canhw {

/// 硬件实例工厂:根据 厂商 -> 设备类型 -> 协议 三层枚举,
/// 查找并创建对应的 IHardware 实现。
///
/// C# 版靠反射扫描程序集里的 HardwareDefAttributes 特性;纯 C++ 版没有运行时反射,
/// 改为「静态注册表」:各厂商在 RegisterXxxHardware() 中显式注册创建器,
/// 上层调用方式与 .NET 版完全一致。
class Factory {
public:
    using Creator = std::function<std::unique_ptr<IHardware>()>;

    /// 注册一个硬件封装(通常由各厂商的 RegisterXxxHardware 调用)。
    /// EqEnum 必须是该厂商自己的设备类型枚举,否则抛 std::invalid_argument
    /// (对应 .NET HardwareDefAttributes 构造函数的 EquipmentTypeMap 强校验)。
    template <typename EqEnum>
    static void Register(HardwareManufacturer manufacturer,
                         EqEnum equipmentType,
                         CommProtocol protocol,
                         Creator creator) {
        ValidateEquipmentType(manufacturer, typeid(EqEnum));
        RegisterRaw(manufacturer, static_cast<int>(equipmentType), protocol,
                    std::move(creator));
    }

    /// 取硬件实例;未注册返回 nullptr(对应 .NET FactoryInstance.GetInstance)
    template <typename EqEnum>
    static std::unique_ptr<IHardware> GetInstance(HardwareManufacturer manufacturer,
                                                  EqEnum equipmentType,
                                                  CommProtocol protocol) {
        return GetInstanceRaw(manufacturer, static_cast<int>(equipmentType), protocol);
    }

    /// 已注册硬件清单:厂商 -> 设备类型 -> 协议集合(对应 .NET GetHardwareMap)
    using HardwareMap =
        std::map<HardwareManufacturer, std::map<int, std::set<CommProtocol>>>;
    static HardwareMap GetHardwareMap();

private:
    static void ValidateEquipmentType(HardwareManufacturer manufacturer,
                                      const std::type_info& actual);
    static void RegisterRaw(HardwareManufacturer manufacturer, int equipmentType,
                            CommProtocol protocol, Creator creator);
    static std::unique_ptr<IHardware> GetInstanceRaw(HardwareManufacturer manufacturer,
                                                     int equipmentType,
                                                     CommProtocol protocol);
};

/// 注册内置硬件(ZLG / PEAK)。
/// 使用静态库时,调用方必须在任何 Factory::GetInstance 之前调用一次(内部幂等),
/// 否则注册表为空。对应 .NET 里由反射自动完成的注册。
void RegisterBuiltinHardware();

} // namespace canhw
