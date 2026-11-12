#pragma once

namespace canhw {

/// 不同硬件、总线协议配置参数的抽象
/// (对应 .NET BusConfigBase;原 ModeBase 的 INotifyPropertyChanged/WPF
///  绑定与 IXmlSerializer 的 XML 持久化,在无 GUI 框架的纯 C++ 版中省略)
struct BusConfigBase {
    virtual ~BusConfigBase() = default;
};

} // namespace canhw
