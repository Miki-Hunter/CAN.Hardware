#include <canhw/Factory.h>

#include <mutex>
#include <utility>

namespace canhw {
namespace {

// 注册表键:厂商 -> 设备类型(int,已由模板按厂商枚举归一) -> 协议
struct HardwareKey {
    HardwareManufacturer manufacturer;
    int equipmentType;
    CommProtocol protocol;

    bool operator<(const HardwareKey& o) const {
        if (manufacturer != o.manufacturer)
            return manufacturer < o.manufacturer;
        if (equipmentType != o.equipmentType)
            return equipmentType < o.equipmentType;
        return static_cast<int>(protocol) < static_cast<int>(o.protocol);
    }
};

// 函数内静态(Meyers singleton):避免跨编译单元静态初始化顺序问题
std::map<HardwareKey, Factory::Creator>& Registry() {
    static std::map<HardwareKey, Factory::Creator> registry;
    return registry;
}

std::mutex& RegistryMutex() {
    static std::mutex m;
    return m;
}

} // namespace

void Factory::ValidateEquipmentType(HardwareManufacturer manufacturer,
                                    const std::type_info& actual) {
    // 对应 .NET HardwareAttributes.EquipmentTypeMap:
    // 某厂商的设备类型必须用该厂商自己的枚举。
    // 未收录的厂商暂不校验(待移植时在此补充)。
    const std::type_info* expected = nullptr;
    switch (manufacturer) {
        case HardwareManufacturer::Zlg:
            expected = &typeid(ZlgDeviceType);
            break;
        case HardwareManufacturer::Peak:
            expected = &typeid(PeakDeviceType);
            break;
        case HardwareManufacturer::Kvaser:
            expected = &typeid(KvaserDeviceType);
            break;
        case HardwareManufacturer::Tosun:
            expected = &typeid(TosunDeviceType);
            break;
        case HardwareManufacturer::Vector:
            expected = &typeid(VectorDeviceType);
            break;
        case HardwareManufacturer::Esd:
            expected = &typeid(EsdDeviceType);
            break;
        case HardwareManufacturer::Ni:
            expected = &typeid(NiDeviceType);
            break;
        case HardwareManufacturer::Etas:
            expected = &typeid(EtasDeviceType);
            break;
        case HardwareManufacturer::Intrepid:
            expected = &typeid(IntrepidDeviceType);
            break;
        case HardwareManufacturer::EbEl:
            expected = &typeid(EbElDeviceType);
            break;
        case HardwareManufacturer::ChuangXin:
            expected = &typeid(ChuangXinDeviceType);
            break;
        case HardwareManufacturer::Toomoss:
            expected = &typeid(ToomossDeviceType);
            break;
        default:
            break;
    }
    if (expected != nullptr && *expected != actual) {
        throw std::invalid_argument(
            std::string("厂商 ") + ToString(manufacturer) +
            " 的设备类型必须使用对应的厂商枚举(" + expected->name() + ")");
    }
}

void Factory::RegisterRaw(HardwareManufacturer manufacturer, int equipmentType,
                          CommProtocol protocol, Creator creator) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry()[HardwareKey{manufacturer, equipmentType, protocol}] = std::move(creator);
}

std::unique_ptr<IHardware> Factory::GetInstanceRaw(HardwareManufacturer manufacturer,
                                                   int equipmentType,
                                                   CommProtocol protocol) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    auto it = Registry().find(HardwareKey{manufacturer, equipmentType, protocol});
    if (it == Registry().end() || !it->second) return nullptr;
    return it->second();
}

Factory::HardwareMap Factory::GetHardwareMap() {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    HardwareMap map;
    for (const auto& entry : Registry()) {
        map[entry.first.manufacturer][entry.first.equipmentType].insert(
            entry.first.protocol);
    }
    return map;
}

} // namespace canhw
