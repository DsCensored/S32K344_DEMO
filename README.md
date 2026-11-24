# S32K344_DEMO

S32K344 Automotive Grade Microcontroller Demo Project

## Project Introduction

This project is demo code for the NXP S32K344 Automotive Grade MCU. It includes the complete Application Software Layer (ASW), Basic Software Layer (BSW), and Microcontroller Abstraction Layer (MCAL).

### Target Hardware and software

- **ECU**: NXP S32K344-172LFP
- **Architecture**: ARM Cortex-M7
- **IDE**: S32 Design Studio 3.6.0
- **Language**: C
- **Complier**: GCC 10.2
- **MCAL config tool**: EB_tresos 29.0.0
- **RTD Pack**: SW32K3_S32M27x_RTD_R21-11_5.0.0(AutoSar 4.7.0)
- **HSE**: HSE_FW_S32K344_0_2_40_4
- **MBD**: Matlab R2022b

### AutoSarNM
- **stand Autosar Network manage**:
	- BSM\PBSM\RMS\RSS\NOS
- **WakeUpSrc**: KL15 WakeUp, CAN WakeUp
- **KeepSrc**: KL15, UDS req(5sec)
- **SilenceWakeUp**: KL30 WakeUp

### UDS
- **Stand UDS SoftWare Update**: Software update must be forward-compatible
- **HSE support**
- **Stand UDS Service**
- **CAN TP / CAN FD**: UDS_CANTP_CANFD_ENABLE 切换经典 CAN（8 字节帧）与 CAN FD；UDS_CANTP_FD_TX_LEN 定义每帧发送长度（8/12/16/20/24/32/48/64）。CAN FD 下接收支持 64 字节帧：长格式单帧（≤62 字节）、连续帧 63 字节、32 位首帧长度
- **22 AB CC**: current HSE AB swap partition (0x41 'A' = low address, 0x42 'B' = high address, 0xFF = unknown)
- **DEM**: `UDS_DEM.c` 只做故障诊断（采样监控 + 故障延时 + 上升沿请求置位 / 下降沿请求恢复，不访问 NVM），`UDS_DTC.c` 只做 DTC 维护（状态字节、老化、扩展数据、快照、NVM 槽位池），全部由请求触发、不轮询；NVM 只写已确认的故障；`0x14` ClearDiagnosticInformation、`0x19` ReadDTCInformation (01/02/03/04/06/0A/14)、`0x85` ControlDtcSetting

###ASIL
- **HSE TRNG**
- **HSE AES128 for UDS 27 service**
- **HSE SHA-256 for flashing image integrity (0x37 / FF01 / FF02)**
- **HSE RSA-2048 public key for 31 01 02 02 image signature check (installed by 31 01 AA01)**
- **HSE AB Swap OTA**: images are programmed into the passive partition (BOOT `0x00600000`, APP `0x00648000`) and activated with `HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK`; `31 01 FF 02` finalises an APP **or BOOT** update (BOOT-only exchanges the two APP digests and keeps the passive APP) and `31 01 AC C1` switches/rolls back without a new download

