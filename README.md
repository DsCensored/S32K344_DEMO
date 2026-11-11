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

###ASIL
- **HSE TRNG**
- **HSE AES128 for UDS 27 service**
- **HSE SHA-256 for flashing image integrity (0x37 / 31 01 02 02 / FF01 / FF02)**

