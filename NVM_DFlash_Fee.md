# 内部 DFlash + Fee 的简易 NVM 方案

## 1. 总体结构

```
应用 / Boot 代码
      │  Nvm_ReadByte / Nvm_WriteByte / Nvm_GetBlockData / Nvm_Commit
      ▼
   Nvm            BSW/include/Nvm.h, BSW/src/Nvm.c（本项目自己实现）
      ▼
   Fee            RTD Fee（EEPROM 模拟，4 个逻辑块 × 256 B）
      ▼
   MemAcc         RTD MemAcc（地址映射 + 作业管理）
      ▼
   Mem_43_INFLS   RTD 内部 Flash 驱动（扇区/页操作，AC 代码在 RAM 执行）
      ▼
   C40            芯片内部 Data Flash，基地址 0x10000000，扇区 8 KB
```

- `MemIf` 在本版 RTD 中只提供类型定义（`MemIf_Types.h` 里的 `MemIf_StatusType` /
  `MemIf_JobResultType`），Fee/MemAcc 的作业状态就是这两个类型。
- 24C64 是 I2C EEPROM，RTD 没有对应的 Mem/Fee 驱动，且 `Mem_43_EXFLS` 只支持
  QuadSPI NOR Flash，因此本方案按你的要求使用**内部 DFlash**。

## 2. EB 工程（S43K344_EB，已把 Mem 工程合并进来）

Fee / MemAcc / Mem_43_INFLS 的配置现在就在主 EB 工程 **`S43K344_EB`** 里
（原先单独建过 `S43K344_EB_Mem`，内容已合并，见本节末尾）。

| 模块 | 配置内容 |
| --- | --- |
| `Mem_43_INFLS` | 9 个 `MemSectorBatch`：0..4 为原有 Code Flash 区间，5..8 为 DFlash `0x10000000/0x10002000/0x10004000/0x10006000`，各 1 个扇区；DFlash 批次读页 1、写页 8；`MemSynchronizeCache = false`（见第 9 节） |
| `MemAcc` | 1 个地址区，4 个子地址区，逻辑地址 0 / 8192 / 16384 / 24576，分别指向批次 5..8 |
| `Fee` | 1 个 ClusterGroup，2 个 Cluster（每个 2 个子地址区）；4 个块，块号 1..4，每块 256 B；`FeeVirtualPageSize=32`、`FeeDataBufferSize=384` |
| Mem 访问代码 | `MemAcErase = MemAcWrite = 0x2042FF00`（链接脚本 `int_sram_fls_rsv` 保留的 256 B），`MemAcLoadOnJobStart = true` |

逻辑容量：**4 × 256 B = 1 KB**；物理占用：4 个 DFlash 扇区 = **32 KB**，
剩余 96 KB DFlash 仍空闲，可继续加块（改 `FeeBlockConfiguration`）或加扇区。

配置源文件（都在 `S43K344_EB` 工程内）：

- `S43K344_EB/config/Mem_43_INFLS.xdm`
- `S43K344_EB/config/MemAcc.xdm`
- `S43K344_EB/config/Fee.xdm`
- `S43K344_EB/.prefs/pref_general.xdm`（模块使能；`Mem_43_INFLS` / `MemAcc` / `Fee` 已追加，
  顺序在 `Mcl` 之后，Fee 依赖 MemAcc 的引用在生成时解析）
- 原有的 `BaseNXP.xdm / Mcu.xdm / Mcl.xdm / Resource.xdm` 保持主工程版本，未用 Mem 工程的同名文件覆盖

### 重新生成 EB 代码

```powershell
$ws = "D:\Order_Git\S32K344_DEMO\.tresos_ws"
New-Item -ItemType Directory -Force $ws | Out-Null
# 工程目录名是 S43K344_EB，但 .project 里的名字是 S32K344_EB，CLI 用后者
& "D:\EB_Client_License_Administrator\EBtresosStudio_EBtresosStudio\bin\tresos_cmd.bat" `
    -data $ws importProject "D:\Order_Git\S32K344_DEMO\S43K344_EB"
& "D:\EB_Client_License_Administrator\EBtresosStudio_EBtresosStudio\bin\tresos_cmd.bat" `
    -data $ws generate S32K344_EB -o "D:\Order_Git\S32K344_DEMO\S43K344_EB\output"
```

生成结果在 `S43K344_EB/output/{include,src}`，与 NVM 相关的文件：

```
include/: Fee_Cfg.h Fee_Features.h MemAcc_Cfg.h MemAcc_CfgDefines.h
          MemAcc_PBcfg.h Mem_43_INFLS_Cfg.h Mem_43_INFLS_CfgDefines.h
          C40_Ip_Cfg.h modules.h
src/:     Fee_Cfg.c MemAcc_Cfg.c MemAcc_PBcfg.c Mem_43_INFLS_Cfg.c C40_Ip_Cfg.c
```

> 合并后 MemAcc 的 PB 配置文件名从 `MemAcc_VS_0_PBcfg.*` 变成 `MemAcc_PBcfg.*`
> （主工程没有配置 post-build 变体 VS_0；两者内容等价，只是去掉了 `_VS_0` 后缀）。
> app/boot 的 `MCAL/EB_GEN` 已同步成新命名，`MemAcc_Cfg.h` 改为 `#include "MemAcc_PBcfg.h"`。
> 如果以后在 tresos 里切回 VS_0 变体，记得同步改回两个工程的 EB_GEN 和构建文件。

> `S43K344_EB_Mem` 保留作参考（含 `make_nvm_cfg.py` 一键配置脚本），
> 日常生成请以 `S43K344_EB` 为准。


## 3. 文件结构（APP / BOOT 一致）

RTD 驱动放置在 `MCAL/RTD_PACK/<模块>_TS_T40D34M50I0R0/{include,src}`，
和工程里其他 RTD 模块的结构完全相同；EB 生成物放在 `MCAL/EB_GEN/{include,src}`；
自己写的 NVM 层放在 `BSW/`。

```
APP: S32K344_S32DS/S32K344
├─ MCAL/RTD_PACK/
│  ├─ Fee_TS_T40D34M50I0R0/        include/Fee.h Fee_Types.h Fee_Cbk.h Fee_InternalTypes.h
│  │                               src/Fee.c
│  ├─ MemAcc_TS_T40D34M50I0R0/     include/MemAcc.h MemAcc_Types.h
│  │                               src/MemAcc.c
│  └─ Mem_43_INFLS_TS_T40D34M50I0R0/
│                                  include/Mem_43_INFLS*.h C40_Ip*.h
│                                  src/Mem_43_INFLS*.c C40_Ip.c C40_Ip_Ac.c
├─ MCAL/EB_GEN/
│  ├─ include/                     Fee_Cfg.h MemAcc_Cfg*.h Mem_43_INFLS_Cfg*.h
│  │                               C40_Ip_Cfg.h MemIf_Types.h Cache_Ip_Cfg_Defines.h
│  └─ src/                         Fee_Cfg.c MemAcc_Cfg.c MemAcc_VS_0_PBcfg.c
│                                  Mem_43_INFLS_Cfg.c C40_Ip_Cfg.c
└─ BSW/
   ├─ include/                     Nvm.h  App_Info.h
   └─ src/                         Nvm.c  App_Info.c
```

Boot 完全对应：

```
BOOT: S32K344_S32DS/S32K344_Bootloader
├─ MCAL/RTD_PACK/Fee_.../{include,src}
├─ MCAL/RTD_PACK/MemAcc_.../{include,src}
├─ MCAL/RTD_PACK/Mem_43_INFLS_.../{include,src}
├─ MCAL/RTD_PACK/Mcl_.../include（原有）+ src/Cache_Ip.c Cache_Ip_HwAcc_ArmCoreMx.c
├─ MCAL/EB_GEN/{include,src}   生成配置
└─ BSW/{include,src}           Nvm.h/Nvm.c、App_Info.h/App_Info.c
```

### 构建集成

| 工程 | 修改点 |
| --- | --- |
| APP | `.cproject` 增加 Fee/MemAcc/Mem_43_INFLS 的 include 路径；`Debug_FLASH/MCAL/RTD_PACK/nvm_stack.mk`（手写，含三个模块 + EB_GEN + BSW/src/Nvm.c 的编译规则）；`Debug_FLASH/makefile` 引入该 mk；`S32K344.args` 增加对应 `.o` |
| BOOT | `.cproject` 增加 Fee/MemAcc/Mem_43_INFLS/Mcl include 路径；`Debug_FLASH/MCAL/RTD_PACK/nvm_stack.mk`（多 Mcl/src 与 Cache）；`Debug_FLASH/src/subdir.mk` 只剩 `main.c`；`BSW/src/subdir.mk` 增加 `App_Info.c`；`S32K344_Bootloader.args` 更新 `.o` 路径 |
| 两者 | `MCAL/BSW_Init.c` 调用 `Nvm_Init()`；链接脚本增加 AC 段/符号（`KEEP(*(.acmem_43_infls_code_rom*))`），RAM 保留 `0x2042FF00..0x2042FFFF` |

> **Cache_Ip**：C40/Mem_43_INFLS 擦写前后要调 `Cache_Ip_*`，APP 的 Mcl 配置已改为
> `MclEnableCache=true`（`S43K344_EB/config/Mcl.xdm`），生成的
> `Cache_Ip_Cfg_Defines.h` 中 `CACHE_IP_IS_AVAILABLE/ARMCOREMX/INSTRUCTION/DATA`
> 为 `STD_ON`，驱动直接用 Mcl 包里的 `Cache_Ip.c`。

> `Debug_FLASH` 是 S32DS 生成的构建目录（`.gitignore` 已忽略）。在 IDE 里重新
> Build 会按 `.cproject` 重新生成 makefile/args（`MCAL` 源目录递归包含新模块），
> 手写的 `nvm_stack.mk` 只用于命令行构建。

## 4. 固定地址信息区（App_Info）

参考 `LIGOO/CHERY_C0V_600E6` 的 `linker_flash.ld`，在 APP 和 BOOT 链接脚本里把 8 个
**16 字节的 u8 数组**固定存放（APP 紧跟 16 字节 Boot 头之后、8 KB 对齐的向量表之前；
BOOT 在自己的镜像头部之后），地址不随代码变化：

| APP 地址 | BOOT 地址 | 段名 | 数组 | 初始内容 |
| --- | --- | --- | --- | --- |
| `0x00448100` | `0x00400100` | `.app_ver_flash` | `App_Info_AppVersion` | `"APP-1.0.0"` |
| `0x00448110` | `0x00400110` | `.app_boot_ver_flash` | `App_Info_BootVersion` | `"BOOT-1.0.0"` |
| `0x00448120` | `0x00400120` | `.app_hw_ver_flash` | `App_Info_HardwareVersion` | `"S32K344-Q172"` |
| `0x00448130` | `0x00400130` | `.app_order_flash` | `App_Info_OrderCode` | `"ORD-DEMO-0001"` |
| `0x00448140` | `0x00400140` | `.app_supplier_flash` | `App_Info_SupplierCode` | `"SUP-NXP"` |
| `0x00448150` | `0x00400150` | `.app_serial_flash` | `App_Info_SerialNumber` | `"SN-0000000001"` |
| `0x00448160` | `0x00400160` | `.app_prod_date_flash` | `App_Info_ProductionDate` | `"2026-10-29"` |
| `0x00448170` | `0x00400170` | `.app_upd_comp_flash` | `App_Info_UpdateComponent` | `"UPD-COMP-0001"` |

链接脚本实现（两个工程的 `.pflash` / `.int_pflash_Boot` 内，按偏移定位）：

```ld
        KEEP(*(.boot_header))
        . = 0x100;
        KEEP(*(.app_ver_flash))
        . = 0x110;
        KEEP(*(.app_boot_ver_flash))
        ...
        . = 0x170;
        KEEP(*(.app_upd_comp_flash))
        . = 0x200;
        . = ALIGN(8192);
```

C 侧（`BSW/src/App_Info.c`）：

```c
__attribute__((section(".app_ver_flash"), used))
const uint8 App_Info_AppVersion[APP_INFO_BLOCK_SIZE] = "APP-1.0.0";
```

读取接口（`BSW/include/App_Info.h`，两个工程各编译一份 `App_Info.c`）：

```c
uint32       App_Info_GetAddress(APP_INFO_APP_VERSION);   /* 本镜像内实际地址 */
const uint8 *p = App_Info_Get(APP_INFO_APP_VERSION);      /* 16 字节 */
uint8 buf[APP_INFO_BLOCK_SIZE];
App_Info_GetCopy(APP_INFO_SERIAL_NUMBER, buf);

/* 跨镜像按固定地址访问 */
#define APP_INFO_ADDRESS(id)       (0x00448100UL + (id) * 16U)   /* APP 镜像 */
#define APP_INFO_BOOT_ADDRESS(id)  (0x00400100UL + (id) * 16U)   /* BOOT 镜像 */
```

生成的 S19 中：`S32K344.s19` 在 `0x448100..0x44817F`、`S32K344_Bootloader.s19`
在 `0x400100..0x40017F` 各是这 8 块内容（两边的 16 字节镜像头分别仍在
`0x448000` / `0x400000`）。两个镜像的字段含义完全一致（块 0 = APP 版本、
块 1 = BOOT 版本、……），工具按同一套偏移即可读取任一镜像；刷写工具可直接改
这些地址；Boot 侧也可以用绝对地址读取 APP 的版本信息（例如显示 APP 版本）。

> 注意：这些块位于 APP 镜像内，属于 Boot 校验 CRC 的范围；如果刷写工具在
> 刷写后改动它们，需要同步更新校验值，或在 Boot 的 CRC 计算中排除该区间。

## 5. NVM 接口

```c
#include "Nvm.h"

typedef enum {
    NVM_BLOCK_BOOT_PARAM = 0,   /* 256 B */
    NVM_BLOCK_APP_PARAM,        /* 256 B */
    NVM_BLOCK_CALIBRATION,      /* 256 B */
    NVM_BLOCK_LOG,              /* 256 B */
    NVM_BLOCK_INVALID = 0xFF
} Nvm_BlockIdType;
```

| 函数 | 作用 |
| --- | --- |
| `Nvm_Init()` | 初始化 MemAcc/Fee，等待启动扫描完成，把 4 块读到 RAM 影子（未写过的块清零） |
| `Nvm_ReadByte(block, offset, &val)` | 按字节读（RAM 影子） |
| `Nvm_WriteByte(block, offset, val)` | 按字节写（只改 RAM，标脏） |
| `Nvm_GetBlockData(block)` | 整块 256 B 指针，可直接字节访问 |
| `Nvm_LoadBlock(block, buf)` / `Nvm_StoreBlock(block, buf)` | 整块读 / 整块写并立即落盘 |
| `Nvm_CommitBlock(block)` / `Nvm_Commit()` | 把脏块写入 DFlash |
| `Nvm_IsBlockDirty(block)` / `Nvm_GetStatus()` / `Nvm_MainFunction()` | 状态查询 / 周期轮询 |

示例：

```c
uint8_t v = 0;
Nvm_ReadByte(NVM_BLOCK_CALIBRATION, 0U, &v);
Nvm_WriteByte(NVM_BLOCK_CALIBRATION, 0U, v + 1U);
Nvm_CommitBlock(NVM_BLOCK_CALIBRATION);     /* 落盘 */

uint8_t *p = Nvm_GetBlockData(NVM_BLOCK_APP_PARAM);
p[10] = 0x5A;
Nvm_CommitBlock(NVM_BLOCK_APP_PARAM);
```

限制：`Nvm_WriteByte` 只改 RAM，必须 `Commit` 才落盘；API 阻塞，勿在中断调用；
首次使用 Fee 会格式化 4 个 DFlash 扇区（几十 ms）。

## 6. 资源占用（当前构建）

| 项目 | text | data | bss |
| --- | --- | --- | --- |
| APP `S32K344.elf` | 85 924 B | 1 504 B | 22 596 B |
| BOOT `S32K344_Bootloader.elf` | 168 124 B | 2 079 B | 36 016 B |

- DFlash：`0x10000000..0x10007FFF`（32 KB）给 Fee，其余 96 KB 空闲。
- SRAM：`0x2042FF00..0x2042FFFF`（256 B）保留给 AC；实际 AC：APP 56 B、BOOT 96 B。
- 信息区：APP `0x00448100..0x0044817F`；BOOT `0x00400100..0x0040017F`（各 8 × 16 B）。
- APP 代码起始（向量表）`0x0044A000`，结束于 `0x0045CF64`；
  BOOT 代码起始（向量表）`0x00402000`，结束于 `0x004290F0`，均未越界。

## 7. 验证建议

1. 编译并烧录后，用调试器/二进制查看 `0x00448100`（APP）和 `0x00400100`（BOOT）
   起的 8 个 16 字节块。
2. 多次复位，`NVM_BLOCK_LOG` 第 0 字节应递增（`ASW_NVM_DEMO_ENABLE=1` 时）。
3. 断电重启后读回相同值即说明 Fee 落盘成功。

## 8. 编译

```powershell
$env:PATH = "D:\NXP\S32DS\build_tools\gcc_v11.4\gcc-11.4-arm32-eabi\bin;" +
            "D:\NXP\S32DS\build_tools\msys32\usr\bin;" + $env:PATH

cd D:\Order_Git\S32K344_DEMO\S32K344_S32DS\S32K344\Debug_FLASH
make all -j8

cd D:\Order_Git\S32K344_DEMO\S32K344_S32DS\S32K344_Bootloader\Debug_FLASH
make all -j8
```

两个工程均编译通过，链接后自动生成 `.s19`。

## 9. 已知问题与修复（重要）

### 9.1 `Nvm_Init` 在 `Nvm_WaitIdle` 里 HardFault（CFSR = IMPRECISERR）

**现象**：`Nvm_Init()` 进入 `Nvm_WaitIdle()` 后触发 HardFault，调用栈为

```
Nvm_WaitIdle → MemAcc_MainFunction → Mem_43_INFLS_MainFunction → C40_Ip_Compare
  → C40_Ip_SynchronizeCache → Cache_Ip_InvalidateByAddr
  → hwAcc_ArmCoreMx_DataCacheInvalidateByAddr
```

`CFSR = 0x00000400`（IMPRECISERR，非精确总线错误），`HFSR = 0x40000000`（FORCED）。

**原因**：`C40_IP_SYNCRONIZE_CACHE = STD_ON` 时，Flash 擦除/校验会对 **DFlash 地址
`0x10000000` 起 8 KB** 做“按地址的 Cache 维护”（`DCIMVAC` 循环）。本工程没有使能
D-Cache（`CCR.DC = 0`，`system.c` 里 `D_CACHE_ENABLE` 未定义），这种针对 Flash 区
的按地址 Cache 维护在 S32K3 上会触发一次非精确总线错误，导致初始化中断。

**修复**：关闭 `Mem_43_INFLS` 的 Cache 同步（APP/BOOT 都要）：

- EB 配置：`S43K344_EB_Mem/config/Mem_43_INFLS.xdm` 里
  `<d:var name="MemSynchronizeCache" type="BOOLEAN" value="false"/>`
- 生成结果：`MCAL/EB_GEN/include/C40_Ip_Cfg.h` 中
  `#define C40_IP_SYNCRONIZE_CACHE (STD_OFF)`

> 注意：如果以后在 `system.c` 里使能了 D-Cache（编译宏 `D_CACHE_ENABLE` /
> `I_CACHE_ENABLE`，需先使能 MPU），不能直接把这里改回 `STD_ON`。Flash 读的
> 一致性应改用“整 Cache 失效”（`SCB->DCISW` 或 `Cache_Ip_Invalidate(CORE, DATA)`），
> 不要对 Flash 地址做按地址的 `DCIMVAC`。

**验证（J-Link 实测）**：
1. 复位后 `Nvm_Init` 返回，`Nvm_Status = NVM_IDLE`，`CFSR/HFSR = 0`；
2. `Nvm_WriteByte` + `Nvm_CommitBlock` 返回 `E_OK`；
3. 复位后 `Nvm_GetBlockData` 读回相同数据（`Nvm_Valid = 1`），说明 Fee 落盘正常。

### 9.2 链接脚本必须收集带后缀的 `.mcal_*.*` 段

工程使用 `-fdata-sections/-ffunction-sections`，RTD 的 MemMap 会把变量放到
`.mcal_data` / `.mcal_bss` / `.mcal_const`，编译后实际段名带符号后缀
（如 `.mcal_data.MemAcc_VS_0_MemApis`）。链接脚本里若只写 `*(.mcal_data)`，
这些段会变成 orphan：既不参与 `.data` 拷贝、也不参与 `.bss` 清零，
表现为 `MemAcc` 函数指针表为 0（HardFault）或驱动静态变量随机值。

APP 与 BOOT 的 `S32K344_flash.ld` 已补：

```ld
*(.mcal_text.*)  *(.mcal_const_cfg.*)  *(.mcal_const.*)
*(.mcal_data.*)  *(.mcal_bss.*)
*(.mcal_data_no_cacheable.*)  *(.mcal_const_no_cacheable.*)  *(.mcal_bss_no_cacheable.*)
KEEP(*(.mcal_shared_data.*))  *(.mcal_shared_bss.*)
```

这些通配不能删，否则重新 Clean 编译后又会出问题。

### 9.3 BOOT 读不到 F190（boot 的 NVM 初始化失败）

**现象**：APP 里 `2E F190` 写入后，APP `22 F190` 能读到，但 `10 02` 进 BOOT 后
`22 F190` 全 0；进 J-Link 看 `Fee_eJobResult = MEMIF_JOB_FAILED`、
`Mem_43_INFLS_pConfigPtr = NULL`、`C40_Ip_pConfigPtr = NULL`。

**原因**：BOOT 工程里 `MCAL/RTD_PACK/Mem_43_INFLS_.../src/Mem_43_INFLS.c` 的
`Mem_43_INFLS_Init()` 判断被写反了：

```c
/* 错误（BOOT 里原来的写法）：传 NULL 反而报错并跳过初始化 */
if (NULL_PTR == ConfigPtr) { ReportDevError(...); }
else { ... 初始化 ... }
```

`MemAcc_Init()` 按 AUTOSAR 约定调用 `InitFunc(NULL_PTR)`（使用预编译配置），
所以 BOOT 每次都走到 DEV 错误分支，`pConfigPtr` 保持 NULL，Fee 的所有作业都失败，
NVM 影子缓冲区保持全 0 → `22 F190` 返回 17 个 0。APP 里同名文件是正确的
`if (NULL_PTR != ConfigPtr)`，所以 APP 正常。

**修复**：BOOT 的 `Mem_43_INFLS_Init()` 改成与 APP 一致：

```c
if (NULL_PTR != ConfigPtr)
{
    Mem_43_INFLS_ReportDevError(0U, MEM_43_INFLS_INIT_ID, MEM_43_INFLS_E_PARAM_POINTER);
}
else
{
    Mem_43_INFLS_pConfigPtr = &Mem_43_INFLS_Config;
    ...
}
```

**验证（USBCAN-2E-U + J-Link 实测）**：

```
APP : 10 03 → 50 03
      31 01 02 03 → 71 01 02 03 00
      10 02 → 50 02              (进入 BOOT)
BOOT: 22 F190 → 62 F1 90 11 22 33 44 55 66 77 88 99 00 11 22 33 44 55 66 77
```

### 9.4 BOOT 超时跳 APP 后 HardFault（0x20020000）

**现象**：正常上电 APP 能跑；`10 02` 进 BOOT 后等 5 s 超时跳回 APP，APP 直接进
`HardFault_Handler`。J-Link 读 `CFSR = 0x00008200`（PRECISERR + BFARVALID），
`BFAR = 0x20020000`（DTCM 栈顶，`int_stack_dtcm` 的结束地址）。

**原因**：`Boot_JumpToApp()` / `Boot_TryJumpToApp()` 里切换 MSP 的写法是

```c
__asm volatile ("msr msp, %0" : : "r" (sp));   /* sp = APP 的栈顶 0x20020000 */
((Boot_AppEntryType)reset)();
```

编译器会把函数的收尾（`ldmia.w sp!, {r4, r5, r6, lr}`）排在 `msr msp` 之后，
于是这条弹栈指令从 **APP 的新栈顶** 0x20020000 取数据，越界访问 DTCM → 精确总线错误。
反汇编（修复前）：

```
402c5a:  msr  MSP, r6
402c5e:  mov  r3, r5
402c60:  ldmia.w sp!, {r4, r5, r6, lr}   ← 用新 MSP 弹栈，访问 0x20020000
402c64:  bx   r3
```

**修复**：新增一个 `naked` 函数，切换 MSP 之后不再有任何栈访问：

```c
static void __attribute__((naked, noreturn)) Boot_StartApp(unsigned int stackPointer,
                                                           unsigned int resetHandler)
{
    __asm volatile ("msr msp, r0\n"
                    "dsb\n"
                    "isb\n"
                    "bx  r1\n");
}
```

两个跳转函数（`Boot_TryJumpToApp`、`Boot_JumpToApp`）都改成最后一句
`Boot_StartApp(sp, reset);`。修复后反汇编：

```
4029c0 <Boot_StartApp>:
  msr  MSP, r0
  dsb  sy
  isb  sy
  bx   r1
```

同时把 `Boot.c` 里 `Mcl_DeInit()` / `Mem_43_INFLS_DeInit()` / `MemAcc_DeInit()`
的声明补上（`#include "CDD_Mcl.h"`、`"MemAcc.h"`、`"Mem_43_INFLS.h"`，函数名是
`Mcl_DeInit` 不是 `Mcl_Deinit`），否则编译报隐式声明/链接错误。

**验证（实测）**：`10 02` → BOOT（`22 F190` 正常）→ 等 7 s 超时 → APP 恢复响应，
`22 F184` 返回 `FF FF...`（APP 的指纹，BOOT 返回 0，可用来区分当前是谁在应答），
J-Link 看 `PC` 在 APP 的 `BSW_OsStart` 主循环、`CFSR/HFSR = 0`。

### 9.5 用 BOOT 的 `22 F184` 区分当前运行的是 APP 还是 BOOT

| DID | APP | BOOT |
| --- | --- | --- |
| `22 F184` | `62 F1 84` + `FF` × 9 | `62 F1 84` + `00` × 9 |
| `22 F190` | NVM 里的 VIN | 同一份 NVM 里的 VIN（修好 9.3 后） |


