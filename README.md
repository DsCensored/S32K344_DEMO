# S32K344_DEMO

S32K344 汽车级微控制器演示项目，基于 AUTOSAR 架构设计。

## 项目简介

本项目是 NXP S32K344 汽车级 MCU 的演示代码，采用 AUTOSAR 架构实现，包含了完整的应用层（ASW）、基础软件层（BSW）和微控制器抽象层（MCAL）。

### 目标硬件

- **微控制器**: NXP S32K344
- **架构**: ARM Cortex-M7
- **开发环境**: S32 Design Studio for ARM

## 软件架构

```
┌─────────────────────────────────────────┐
│            ASW (应用层)                  │
│  ┌─────────────┐  ┌──────────────────┐  │
│  │   Model     │  │    UserCode      │  │
│  └─────────────┘  └──────────────────┘  │
├─────────────────────────────────────────┤
│          RTE (运行时环境)                │
├─────────────────────────────────────────┤
│            BSW (基础软件层)              │
│  ┌────────┐ ┌──────┐ ┌─────┐ ┌───────┐ │
│  │  CAN   │ │ UART │ │ NVM │ │  ...  │ │
│  └────────┘ └──────┘ └─────┘ └───────┘ │
├─────────────────────────────────────────┤
│         MCAL (微控制器抽象层)            │
│  ┌────────┐ ┌──────┐ ┌─────┐ ┌───────┐ │
│  │  CAN   │ │ GPT  │ │ ICU │ │  ...  │ │
│  └────────┘ └──────┘ └─────┘ └───────┘ │
└─────────────────────────────────────────┘
```

## 目录结构

### AutoSarNm
AUTOSAR 网络管理模块实现，包含 CAN 网络管理功能。

- `NMmain.c/h` - 网络管理主程序
- `NM.dbc` - CAN 数据库文件
- `NM.ini` - 网络管理配置文件

### S32K344_S32DS/S32K344

主工程目录，包含以下主要模块：

#### ASW (应用层)
- **Model** - Simulink 模型生成的代码
- **UserCode** - 用户应用代码
  - `ASW_CanCallback.c` - CAN 回调处理
  - `NvmApp.c/h` - NVM 应用接口
  - `UserCode.c/h` - 用户任务入口

#### AutoSarNM
AUTOSAR 网络管理实现模块。

#### UDS
UDS 诊断协议栈
- 具有完整的DCM协议栈功能，能够实现UDS刷写的功能

#### BSW (基础软件层)
- **include** - 头文件目录
  - `App_Info.h` - 应用信息定义
  - `BSW_CanCallback.h` - CAN 回调接口
  - `BSW_UART.h` - UART 接口
  - `Nvm.h` - NVM 接口
  - `RTE.h` - 运行时环境接口

- **src** - 源代码目录
  - `App_Info.c` - 应用信息管理
  - `BSW_CanCallback.c` - CAN 回调实现
  - `BSW_UART.c` - UART 驱动
  - `Nvm.c` - NVM 管理
  - `RTE.c` - 运行时环境
  - `BSW_WDG.c` - 看门狗驱动

#### MCAL (微控制器抽象层)
包含以下 MCAL 驱动：
- CAN (FlexCAN)
- UART/LPUART
- GPT (PIT, STM, eMIOS)
- ICU (ICU, eMIOS ICU, Wkpu)
- Memory (Fee, MemAcc, DFlash)
- 以及其他外设驱动

## 主要功能

### CAN 通信
- CAN FD 支持
- CAN 回调处理
- 网络管理集成
- 接收/发送通道管理
- UDS诊断协议栈

### 网络管理 (AutoSarNM)
- BSM (Bus Sleep Mode)
- RMS (Repeat Message State)
- RSS (Ready Sleep State)
- PBSM (Prepare Bus Sleep Mode)
- 快速/慢速状态机

### NVM (非易失性存储)
- 内部 DFlash + Fee 方案
- 数据块管理
- 块状态跟踪
- 应用程序数据存储

### 串口通信
- UART6 DMA 接收
- 帧检测
- 超时处理

### 定时器
- 多周期任务调度 (2ms, 5ms, 10ms, 20ms, 50ms, 100ms, 1000ms)
- 硬件定时器支持

## 编译说明

### 开发环境
- S32 Design Studio for ARM
- NXP S32K344 芯片支持包

### 编译步骤
1. 使用 S32 Design Studio 打开工程
2. 选择合适的构建配置 (Debug/Release)
3. 编译工程

## 配置文件

### 固定地址信息区
- APP 镜像地址: `0x00448100 + (id) * 16`
- BOOT 镜像地址: `0x00400100 + (id) * 16`

### CAN 配置
- CAN 通道配置
- 波特率设置
- 过滤器配置

## 注意事项

1. 本项目为演示代码，实际使用时需根据具体需求进行配置
2. NVM 方案使用内部 DFlash + Fee，需确保 Flash 空间充足
3. 网络管理功能需根据实际 CAN 网络拓扑进行配置

## 文档

详细的技术文档请参考：
- `NVM_DFlash_Fee.md` - NVM 实现方案及已知问题说明

## 许可证

本项目代码仅供学习和开发使用。