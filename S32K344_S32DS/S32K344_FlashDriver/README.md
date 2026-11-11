# S32K344_FlashDriver

UDS 刷写用的 Flash Driver，链接在 **ITCM (0x00000000)**。镜像本身不是可启动镜像，
不需要 Reset_Handler / SystemInit，也不占用 program flash。Bootloader 通过
`0x34 / 0x36 / 0x37` 把 `.bin` 下载到 ITCM，然后通过镜像头部的 `entry` 调用。

Bootloader 不再链接任何 C40 / Mem_43_INFLS 代码；擦除、写入、校验全部由本驱动完成。

## 目录结构

```
src/main.c                                 driver 入口 + 命令分发
src/flash_driver.h                         boot 与 driver 的接口（magic / 命令 / 返回值）
src/C40_Ip.c, src/C40_Ip_Ac.c              C40 flash IP
src/Mem_43_INFLS*.c                        AUTOSAR Mem 驱动（扇区映射、异步 job）
src/Cache_Ip*.c                            缓存维护（当前配置下不参与擦写路径）
Project_Settings/Linker_Files/
        S32K344_flashdriver_ram.ld         ITCM 链接脚本（VMA=0x0，无 AT()）
```

## 镜像布局

| 地址 | 内容 |
| ---- | ---- |
| 0x0000 | `FlashDriver_HeaderType`：magic(`0x48534C46` "FLSH") / version(`0x00010001`) / entry / reserved |
| 0x0010 | `.text` / `.rodata` / `.data`（全部在 ITCM） |
| ... | `.bss`（NOLOAD，不在 `.bin` 里，INIT 命令里清零） |

当前 `.bin` 约 14 KB，`.bss` 约 0.8 KB，`__bss_end` 远小于 32 KB ITCM 下载窗口。

## 命令

| 命令 | 值 | 说明 |
| ---- | -- | ---- |
| `FLASHDRIVER_CMD_INIT` | 0x01 | 清 `.bss`，`Mem_43_INFLS_Init()`（内部初始化 C40） |
| `FLASHDRIVER_CMD_ERASE` | 0x02 | 擦除 `[addr, addr+len)`，自动按 8 KB 扇区向下/向上取整 |
| `FLASHDRIVER_CMD_WRITE` | 0x03 | 从 `buf` 写入，地址/长度必须是 128 B 的整数倍 |
| `FLASHDRIVER_CMD_VERIFY` | 0x04 | 逐字节比较 flash 与 `buf` |
| `FLASHDRIVER_CMD_DEINIT` | 0x05 | `Mem_43_INFLS_DeInit()` |

返回值：`0` 成功，`-1` 参数错误，`-2` 未 INIT，`-4` flash job 失败，
`-5` 校验失败，`-6` job 超时。

## 关键实现说明

- **扇区映射**：擦除走 `Mem_43_INFLS_Erase()`，由 Mem/C40 用
  `C40_Ip_GetSectorNumberFromAddress()` 计算虚拟扇区。不能直接用
  `(addr - 0x00400000) / 0x2000`：C40 的虚拟扇区从 data flash 的 16 个扇区之后
  才开始，0x00400000 对应虚拟扇区 16。
- **Domain ID**：C40 写 PEADR 后要等待 `MCR.PEID == DomainIdValue`。CM7 内核的
  domain 是 **0**，使用 1 会永久卡在 `C40_Ip_MainInterfaceWriteJobAddress()`。
- **访问代码（AC）**：`MEM_43_INFLS_AC_LOAD_ON_JOB_START` 关闭。驱动本身就在
  ITCM 执行，不需要把 AC 拷到 SRAM；关闭后 C40 job 走异步模式，由
  `Mem_43_INFLS_MainFunction()` 轮询完成。
- **擦写校验/缓存同步**：`C40_IP_ERASE_VERIFICATION_ENABLED`、
  `C40_IP_PROGRAM_VERIFICATION_ENABLED`、`C40_IP_SYNCRONIZE_CACHE` 关闭。
  Bootloader 运行时 D-Cache 关闭，校验由 UDS 层（`0x37` 的 HSE SHA-256、`FF01`）
  完成。
- **中断**：Boot 在调用驱动前后 `cpsid i` / 恢复 PRIMASK。C40 擦写期间
  program flash 不可取指，中断服务程序若从 flash 取指会挂死。
- **超时**：`FlashDriver_WaitJob()` 对 PENDING 有循环上限，避免硬件异常时死等。

## Bootloader 侧

`Boot_Flash.c` 只做三件事：

1. 校验 ITCM 0x0 的 magic/version/entry；
2. 首次调用前使能 ITCM 并 `ICIALLU` 无效 I-Cache；
3. 在关中断临界区里调用 `entry(cmd, addr, len, buf)`。

`Boot_Program.c` 的 `0x34/0x36/0x37` 把驱动下载到 ITCM、把 APP 下载到
备份区；`FF00` 每次 DCM poll 擦一个 8 KB 扇区（返回 NRC 0x78），`FF01` 校验，
`FF02` 每 8 KB 把备份区拷贝到运行区并重新计算 SHA-256。

镜像完整性全部由 **HSE 的 SHA-256** 完成（`MCAL/HSE/hse_hash.c`，参考
`S32K3_HSE_DemoExamples` 的 `hse_host_hash.c`），不再使用软件 CRC32：

- `0x37` 正响应：`77 <SHA-256:32>`；
- `31 01 02 02`：`31 01 02 02 <SHA-256:32>`，与最后一次 `0x37` 的摘要比较；
- `FF01`：对备份区重新计算 SHA-256；
- `FF02`：对运行区重新计算 SHA-256。

## 构建

必须使用 **NXP GCC 10.2 for Arm 32-bit Bare-Metal**（工程 `.cproject` 指定的
toolchain）。不能用 GCC 9.2：9.2 的默认 `libc.a` 是 ARM 指令集，而 Cortex-M7
只支持 Thumb-2，链接后会以 ARM 状态调用 `memset`/`memcpy` 导致 HardFault。

S32DS 中选择 `Debug_FLASH` 编译，postbuild 生成：

```
arm-none-eabi-objcopy -O binary S32K344_FlashDriver.elf S32K344_FlashDriver.bin
```

命令行构建示例（PowerShell）：

```powershell
$env:PATH = 'D:\NXP\S32DS\build_tools\gcc_v10.2\gcc-10.2-arm32-eabi\bin;' +
            'D:\NXP\S32DS\build_tools\msys32\usr\bin;' + $env:PATH
& 'D:\NXP\S32DS\build_tools\msys32\usr\bin\make.exe' `
  -C 'D:\Order_Git\S32K344_DEMO\S32K344_S32DS\S32K344_FlashDriver\Debug_FLASH' all
```

## 刷写验证

`tools/flash_test.py` 完成完整流程：

```
10 02 -> 50 02
34/36/37 driver -> ITCM 0x0 (0x37 返回 HSE SHA-256，32 字节)
31 01 FF00 0x00648000 0x12000 (整段擦除，期间多次 0x78)
34/36/37 app    -> 备份区 0x00648000
31 01 FF01 -> 71 01 FF 01 00
31 01 FF02 -> 71 01 FF 02 00
11 01 -> 51 01
22 F190 -> 应用版本
```

已实际验证（J-Link + USBCAN-2E-U，250 kbit/s）：

- `app_v2.bin`：FF00 整段擦除 + 下载 + FF01 + FF02 成功，回读运行区/备份区
  与 `app_v2.bin` 逐字节一致，复位后 `F190 = WORLDWIDEVIN00002`；
- `app_v1.bin`：同样流程成功，回读一致，`F190 = WORLDWIDEVIN00001`；
- `--sector-erase`（每扇区一条 FF00）和整段 FF00（0x78 续期）两条路径均通过。
