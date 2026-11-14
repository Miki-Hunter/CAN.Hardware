cmake_minimum_required(VERSION 3.16)
project(CANHardwareCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码含中文注释/字符串,统一按 UTF-8 解析(MSVC 默认按系统 GBK 读会报 C4819 甚至语法错)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ------------------------------------------------------------------
# canhw 静态库:核心抽象 + 12 家厂商驱动适配
# 纯标准 C++17,不依赖 Qt / Boost 等任何第三方框架
# ------------------------------------------------------------------
add_library(canhw STATIC
    # ---- 核心 ----
    src/Frame.cpp
    src/Factory.cpp
    src/RegisterBuiltin.cpp
    src/DynamicLibrary.cpp
    # ---- ZLG(周立功)----
    src/zlg/ZlgApi.cpp
    src/zlg/ZlgUsbCanWrapper.cpp
    src/zlg/ZlgCanFdWrapper.cpp
    src/zlg/ZlgRegister.cpp
    # ---- PEAK(PCAN-Basic)----
    src/peak/PeakApi.cpp
    src/peak/PeakDiscovery.cpp
    src/peak/PeakCanWrapper.cpp
    src/peak/PeakCanFdWrapper.cpp
    src/peak/PeakRegister.cpp
    # ---- Kvaser(canlib)----
    src/kvaser/KvaserApi.cpp
    src/kvaser/KvaserCanWrapper.cpp
    src/kvaser/KvaserCanFDWrapper.cpp
    src/kvaser/KvaserRegister.cpp
    # ---- 创芯科技 ChuangXin(ControlCAN / ControlCANFD)----
    src/chuangxin/ChuangXinCanApi.cpp
    src/chuangxin/ChuangXinCanFdApi.cpp
    src/chuangxin/ChuangXinCanWrapper.cpp
    src/chuangxin/ChuangXinCanFdWrapper.cpp
    src/chuangxin/ChuangXinRegister.cpp
    # ---- 同星 Tosun(libTSCAN)----
    src/tosun/TosunApi.cpp
    src/tosun/TosunCanWrapper.cpp
    src/tosun/TosunCanFdWrapper.cpp
    src/tosun/TosunRegister.cpp
    # ---- 图莫斯 Toomoss(USB2XXX)----
    src/toomoss/ToomossApi.cpp
    src/toomoss/ToomossCanWrapper.cpp
    src/toomoss/ToomossCanFdWrapper.cpp
    src/toomoss/ToomossRegister.cpp
    # ---- Vector(vxlapi64)----
    src/vxl/VectorApi.cpp
    src/vxl/VectorCanWrapper.cpp
    src/vxl/VectorCanFdWrapper.cpp
    src/vxl/VectorRegister.cpp
    # ---- ESD(ntcan)----
    src/esd/EsdApi.cpp
    src/esd/EsdCanWrapper.cpp
    src/esd/EsdCanFdWrapper.cpp
    src/esd/EsdRegister.cpp
    # ---- NI-XNET(nixnet)----
    src/ni/NiApi.cpp
    src/ni/NiCanWrapper.cpp
    src/ni/NiCanFdWrapper.cpp
    src/ni/NiRegister.cpp
    # ---- ETAS BOA V2(dll-ocdProxy / dll-csiBind)----
    src/etas/EtasBoaApi.cpp
    src/etas/EtasCanWrapper.cpp
    src/etas/EtasCanFdWrapper.cpp
    src/etas/EtasRegister.cpp
    # ---- Intrepid neoVI(icsneo40)----
    src/intrepid/IntrepidApi.cpp
    src/intrepid/IntrepidCanWrapper.cpp
    src/intrepid/IntrepidCanFdWrapper.cpp
    src/intrepid/IntrepidRegister.cpp
    # ---- EbEl FlexCard(fcbase,仅经典 CAN)----
    src/ebel/EbElApi.cpp
    src/ebel/EbElCanWrapper.cpp
    src/ebel/EbElRegister.cpp
)

target_include_directories(canhw
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # 对外头文件 <canhw/...>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src       # 驱动内部头文件
)

find_package(Threads REQUIRED)
target_link_libraries(canhw PUBLIC Threads::Threads)

if(WIN32)
    # ETAS BOA 安装路径查注册表(RegGetValueA)
    target_link_libraries(canhw PUBLIC advapi32)
else()
    # POSIX dlopen
    target_link_libraries(canhw PUBLIC ${CMAKE_DL_LIBS})
endif()

# ------------------------------------------------------------------
# 演示程序
# ------------------------------------------------------------------
add_executable(canhw_demo demo/main.cpp)
target_link_libraries(canhw_demo PRIVATE canhw)
