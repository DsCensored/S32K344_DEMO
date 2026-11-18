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
- **22 AB CC**: current HSE AB swap partition (0x41 'A' = low address, 0x42 'B' = high address, 0xFF = unknown)

###ASIL
- **HSE TRNG**
- **HSE AES128 for UDS 27 service**
- **HSE SHA-256 for flashing image integrity (0x37 / FF01 / FF02)**
- **HSE RSA-2048 public key for 31 01 02 02 image signature check (installed by 31 01 AA01)**
- **HSE AB Swap OTA**: images are programmed into the passive partition (BOOT `0x00600000`, APP `0x00648000`) and activated with `HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK`; `31 01 FF 02` finalises an APP **or BOOT** update (BOOT-only exchanges the two APP digests and keeps the passive APP) and `31 01 AC C1` switches/rolls back without a new download

