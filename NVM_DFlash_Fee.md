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

> 注意：这些块位于 APP 镜像内，属于 Boot 校验 SHA-256 的范围；如果刷写工具在
> 刷写后改动它们，需要同步更新校验值，或在 Boot 的哈希计算中排除该区间。

## 5. NVM 接口

每个逻辑块 256 B，布局固定为：

```
块偏移 0..3   : 初始化标志 0x55AA55AA（U32，小端）
块偏移 4..254 : 应用数据（251 B，MAX_NVM_LEN = NVM_BLOCK_DATA_SIZE）
块偏移 255    : 校验和（字节 0..254 的 8 位累加和）
```

应用侧的 `BlockXData` 枚举从 0 开始表示**数据区**偏移，`Nvm` 内部访问
DFlash 时统一加 `NVM_BLOCK_DATA_OFFSET`（=4）；初始化标志和校验字节对
`NvmApp`（ASW）不开放，ASW 只通过 `NvmApp` 读写数据。

**DID 不再各自加 magic/checksum**：F190（以及其它读写 NVM 的 DID）直接使用
块级别的初始化标志 + 块校验和，读写时不做任何记录级校验；未写入的数据统一
为 `0xFF`（DFlash 擦除态）。

`Nvm_Init()` 的行为：

- 初始化标志不在位（新片、随机数据、旧布局）→ **立刻创建一个全新的块**，
  内容全部 `0xFF`，并写入 DFlash；
- 标志在位但**校验和与内容不匹配** → 只把 `Nvm_GetChecksumErrorFlags()`
  的对应位置 1（bit0=块0 … bit3=块3），**不修复、不处理**，由应用层决定；
  块数据按读到的内容保留。

写 `0xF190`（`Nvm_F190_Write()`）：

- 只检查当前 F190 的 17 个字节是否都为 `0xFF`，只要有一个不是 `0xFF`
  就拒绝写入（已经写过了）；
- 只改这 17 个字节，块内其它内容保持不变（一个块可以存多个 DID）。

读 `0xF190`（`Nvm_F190_Read()`）：

- 不检查块有效性，直接返回当前 17 字节；没写过的就是 17 × `0xFF`。

休眠前（`NM_EnterSleepMode()`）：

- 调用 `Nvm_Flush()`：把每个块从 DFlash 读回、和 RAM 影子比较，不一致就
  重新写入，直到一致（最多重试 `NVM_FLUSH_RETRIES` 次），保证掉电前数据
  真正落盘。

> 关于「原地写入」：目前持久化仍走 Fee，`Commit` 时 Fee 会写新页/新块，
> 所以 0x0F→0x0E 这类按位写入仍然会占用新的 Fee 页。要实现真正的原地编程
> 需要改 Fee（或绕过 Fee 直接用 Mem 驱动写物理地址），按要求这次不改。
> 现在只把未用数据统一成 `0xFF`，为以后改成原地写入留好数据格式。

```c
#include "Nvm.h"

typedef enum {
    NVM_BLOCK_0 = 0,   /* 256 B = 4 B 标志 + 251 B 数据 + 1 B 校验和 */
    NVM_BLOCK_1,
    NVM_BLOCK_2,
    NVM_BLOCK_3,
    NVM_BLOCK_INVALID = 0xFF
} Nvm_BlockIdType;
```

| 函数 | 作用 |
| --- | --- |
| `Nvm_Init()` | 初始化 MemAcc/Fee，读 4 块；缺标志的块立即创建（0xFF 内容），校验和错的块只置错误标志 |
| `Nvm_GetChecksumErrorFlags()` | 4 个错误位（bit0..bit3），仅在初始化时检测 |
| `Nvm_ReadByte(block, offset, &val)` | 按字节读（offset 为数据区偏移 0..250） |
| `Nvm_WriteByte(block, offset, val)` | 按字节写（只改 RAM，标脏） |
| `Nvm_GetBlockData(block)` | 数据区（251 B）指针，可直接字节访问 |
| `Nvm_LoadBlock(block, buf)` / `Nvm_StoreBlock(block, buf)` | 读/写 251 B 数据区；`StoreBlock` 立即落盘 |
| `Nvm_CommitBlock(block)` / `Nvm_Commit()` | 把脏块写入 DFlash（自动写标志和校验和） |
| `Nvm_Flush()` | 休眠前使用：DFlash 与 RAM 影子逐块比对，不一致就重写直到一致 |
| `Nvm_GetBlockInvalid(block)` / `Nvm_GetBlockXInvalid()` | 整块是否无效 |
| `Nvm_InvalidateBlock(block)` | 在 RAM 里作废一个块（创建失败回滚用，不写 Flash） |
| `Nvm_IsBlockValid(block)` / `Nvm_IsBlockDirty(block)` / `Nvm_GetStatus()` / `Nvm_MainFunction()` | 状态查询 / 周期轮询 |

示例：

```c
uint8_t v = 0;
Nvm_ReadByte(NVM_BLOCK_2, 0U, &v);          /* 无效块返回 E_NOT_OK */
Nvm_WriteByte(NVM_BLOCK_2, 0U, v + 1U);     /* 首次写入会创建该块 */
Nvm_CommitBlock(NVM_BLOCK_2);               /* 落盘 */

uint8_t *p = Nvm_GetBlockData(NVM_BLOCK_1); /* 251 B 数据区 */
p[10] = 0x5A;
Nvm_CommitBlock(NVM_BLOCK_1);
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

### 9.6 BOOT 的 0x37（RequestTransferExit）HardFault

**现象**：刷写 flash driver 时，`34`/`36` 正常，`37` 直接 HardFault；栈回溯
`Boot_FlashDriverValid()` ← `BootProg_TransferExit()` ← `UDS_DcmTransferExit()`；
`CFSR = 0x00010000`（UNDEFINSTR）。

**原因**：BOOT 用 `-O3` 编译，而驱动的基地址是 0：

```c
#define BOOT_DRIVER_BASE 0x00000000UL       /* ITCM */
return (const Boot_FlashDriverType *)BOOT_DRIVER_BASE;
...
drv->Magic
```

`drv` 是常量 0，C 语言里解引用 NULL 指针是未定义行为，GCC 直接把这段代码变成
陷阱指令（反汇编）：

```
402e6e: ldr  r3, [r3, #0]      ; r3 = *(0x0)
402e70: udf  #255              ; ← 未定义指令 → UNDEFINSTR
```

**修复**：

1. `Boot_Flash.c` 把基地址放在 volatile 变量里，编译器无法证明它是 0：

```c
static volatile unsigned int BootFlash_Base = BOOT_DRIVER_BASE;

const void *Boot_FlashRamBase(void)
{
    return (const void *)(unsigned int)BootFlash_Base;
}

static const Boot_FlashDriverType *BootFlashHeader(void)
{
    return (const Boot_FlashDriverType *)(unsigned int)BootFlash_Base;
}
```

2. `Boot_Program.c` 的 RAM 下载目标改成 `(unsigned int)Boot_FlashRamBase()`，
   避免 `memcpy((void *)0, ...)` 同样被当成未定义行为优化。
3. `37` 的响应原来是 `*rspLen = 1`（只回 `77`），而 DCM 会把小于 2 字节的响应补到
   2 字节，多出一个旧字节；当时改为 `77 + CRC32(4 字节)`，与上位机/测试脚本一致。
   （当前版本已进一步改为 `77 + SHA-256(32 字节)`，见 9.7；下面的实测记录是
   CRC32 时代的历史数据。）

> 注意：`HseHash_Sha256Region()` 同样不能对地址 0 做 NULL 判断，`Boot_Program.c`
> 的 `BootProg_HashRegion()` 因此也通过 volatile 变量把 `base` 转成指针。

**验证（USBCAN-2E-U 实测）**：

```
10 03 → 50 03
31 01 02 03 → 71 01 02 03 00
10 02 → 50 02
27 03 → 67 03 12 36 56 64
27 04 → 67 04                        (key = seed ^ 0xA5A5A5A5)
34 00 44 00000000 000017E8 → 74 20 01 F4
36 × 25 → 76 xx
37 → 77 18 4F 2D 2A                  (= CRC32 of S32K344_FlashDriver.bin)
```

之后等 5 s 超时，APP 正常接管（`22 F184` 返回 `FF FF...`）。

### 9.7 刷写完整性校验改为 HSE SHA-256（替代软件 CRC32）

需求：刷写流程不再用 CRC 校验下载内容，改用 HSE 的 SHA-256（参考
`D:\NXP\S32K3_HSE_DemoExamples_1_0_0\S32K3_HSE_DemoExamples`，主要是
`Template/S32K344_DemoAppTemplate/framework/host_crypto_helper/hse_host_hash.c`
的 `HashDataStartStreamSrv/UpdateStreamSrv/FinishStreamSrv`）。

**改动文件**

| 文件 | 内容 |
| --- | --- |
| `MCAL/HSE/hse_hash.c/.h` | 新增：HSE HASH 服务封装（SHA-256，START/UPDATE/FINISH 流式） |
| `ASW/UserCode/Boot_Program.c/.h` | 删除软件 CRC32，改为对“已落盘镜像”重新计算 SHA-256 |
| `UDS/UDS_DcmCfg.c` | `0x37` 回 32 字节摘要；`31 01 02 02` 收发 32 字节摘要 |
| `ASW/UserCode/Boot.c/.h` | 删除已不再使用的 `Boot_Crc32()` / `Boot_Crc32Update()` |

**UDS 报文变化**

```
0x37 正响应 : 77 <SHA-256:32>                      （33 字节）
0x31 01 02 02: 31 01 02 02 <SHA-256:32> -> 71 01 02 02 <status>
```

> 注：`31 01 02 02` 后续已升级为 RSA-2048 签名校验，见 9.8；上面的 32 字节
> 明文摘要格式仅作为 `BOOT_RSA_ALLOW_PLAIN_HASH=1` 时的兼容路径保留。

`FF01` / `FF02` 的 result 码含义不变（只把 CRC 换成 SHA-256），并新增
`0x14`（FF01：HSE 计算失败）与 `0x06`（FF02：HSE 计算失败）用于区分
“摘要不一致”和“HSE 服务失败”。

**实现要点**

1. `0x37` 不再对 CAN 收到的字节做增量校验，而是在结束时由 HSE 对**实际存储
   的镜像**做 SHA-256：FlashDriver 读 ITCM `0x00000000`，APP 读备份区
   `0x00648000`，长度就是 `0x34` 请求的 size。这样连 Flash 写入错误也能发现
   （原来的 RAM 增量 CRC 只能证明“收到的字节没错”）。
2. HSE 流式 SHA-256 要求 UPDATE 长度为 64 的整数倍、FINISH 可为任意长度，
   因此 `hse_hash.c` 用 2 KB 的 non-cacheable 暂存区把任意区间切成
   “整数个 64 字节块 + 末尾不足 64 字节”两部分；调用者无需关心对齐。
3. HSE 读不到 M7 的 DTCM（栈所在），所有描述符/输入/输出都放在
   `.non_cacheable_data`（`int_sram_no_cacheable`），并在每次请求前后做
   D-Cache clean/invalidate。
4. 地址 `0x00000000` 是 ITCM，**不是**空指针：`HseHash_*` 因此不对输入指针
   做非空判断，`BootProg_HashRegion()` 用 volatile 变量保存基地址，避免
   `-O3` 把常量 0 优化成 `udf`（与 9.6 的 0x37 HardFault 同一个坑）。
5. `FF02` 仍然是每次 DCM poll 拷贝 8 KB（回 0x78），只有最后一块拷贝完成后
   才整段回读运行区算 SHA-256 并与下载摘要比较。

**编译验证**（与 IDE 相同的工具链）：

```powershell
$env:PATH = 'D:\NXP\S32DS\build_tools\gcc_v10.2\gcc-10.2-arm32-eabi\bin;' +
            'D:\NXP\S32DS\build_tools\msys32\usr\bin;' + $env:PATH
& 'D:\NXP\S32DS\build_tools\msys32\usr\bin\make.exe' `
  -C 'D:\Order_Git\S32K344_DEMO\S32K344_S32DS\S32K344_Bootloader\Debug_FLASH' all
```

编译无警告；`arm-none-eabi-size` 结果 `text 110584 / data 6568 / bss 32048`。
HSE 序列与摘要值需要在实板上用总线工具确认（`0x37` 的 32 字节摘要应与上位机
对同一 `.bin` 计算的 SHA-256 完全一致）。

### 9.8 0x31 01 02 02 改为 RSA-2048 公钥验签

需求：把 `.ssh/S32K344_RSA.pub` 对应的 RSA-2048 公钥装入 bootloader，并通过
`31 01 AA01` 安装到 HSE；刷写时 `31 01 02 02` 用该公钥解出上位机送来的
SHA-256，再与 `0x37` 计算出的摘要比较。私钥只在上位机使用，不进入 ECU。

**改动文件**

| 文件 | 内容 |
| --- | --- |
| `MCAL/HSE/hse_rsa_key.c/.h` | 新增：RSA-2048 模数（256 字节大端）+ e=65537 数组 |
| `MCAL/HSE/hse_rsa.c/.h` | 新增：HSE 公钥导入、RSA 解密/验签、PKCS#1 v1.5 去填充 |
| `MCAL/HSE/hse_aes.c` | NVM/RAM key catalog 增加 group 1（RSA_PUB，2048 bit） |
| `UDS/UDS_DcmCfg.c` | AA01 格式化后导入 AES + RSA 公钥；0202 按报文长度分发 |
| `ASW/UserCode/Boot_Program.c/.h` | 新增 `BootProg_CheckIntegritySignature()` 与 `BOOT_ST_CHK_RSA` |

**UDS 报文变化**

```
31 01 AA01                         -> 71 01 AA01 <status>
                                      0 = 成功；1 = AES/格式化失败；2 = RSA 公钥导入失败
31 01 02 02 <RSA-2048 签名:256>    -> 71 01 02 02 <status>
                                      0 = 校验通过；1 = 摘要不一致；2 = 无下载；3 = RSA/HSE 失败
```

**实现要点**

1. HSE key catalog 布局：
   - NVM group 0：AES-128 slot 0/1（0x27 level 1/2）；
   - NVM group 1：RSA_PUB slot 0（2048 bit）。
   RAM catalog 保持相同 group 结构（RSA 槽不使用）。
2. RSA 公钥以 `HSE_KEY_TYPE_RSA_PUB` 导入，`keyFlags = ENCRYPT | VERIFY`，
   `keyCounter = 1`（AA01 刚格式化过 catalog）。
3. `HseRsa_VerifyDigest()` 依次尝试三条 HSE 路径，覆盖常见上位机实现：
   - RSAES-PKCS1-v1_5 解密（公钥直接解出 32 字节 SHA-256）；
   - RSA NO_PADDING 解密 + 软件去 PKCS#1 v1.5 填充（兼容 OpenSSL
     `RSA_private_encrypt()`，type 1/type 2 都支持）；
   - HSE SIGN / VERIFY（`bInputIsHashed=TRUE`，标准 RSASSA-PKCS1-v1_5）。
   注意：部分 HSE 固件不允许用 RSA 公钥做 RSA CIPHER 解密（公钥只支持
   encrypt/verify）。如果实板返回 `HSE_SRV_RSP_KEY_INVALID`，上位机应改用
   标准 `RSA_sign(SHA-256)`（带 DigestInfo 的 RSASSA-PKCS1-v1_5），第三条
   路径即可完成校验。
4. 解出的明文既可以是裸 SHA-256（32 字节），也可以是带
   `30 31 30 0D ... 04 20` DigestInfo 前缀的 51 字节标准签名内容。
5. 为兼容当前仍发送 32 字节明文摘要的上位机，`Boot_Program.h` 提供
   `BOOT_RSA_ALLOW_PLAIN_HASH`（默认 1）；量产时应改为 0，只接受 RSA 签名。
6. `hse_rsa.c` 的共享缓冲全部放在 `.non_cacheable_data`，与 `hse_aes.c` /
   `hse_hash.c` 一致；HSE 不能访问 M7 DTCM。

**编译验证**：`Debug_FLASH` 下 `make all` 通过（GCC 10.2，无警告）。

### 9.9 启动时校验 APP 的 SHA-256（NVM Block3）

需求：

1. `31 01 FF02`（`BootProg_CopyStep`）**搬运前**，把 APP 的 SHA-256 和文件长度
   存进 NVM；
2. 启动时（`Boot_TryJumpToApp`）初始化 NVM、读出 SHA-256 和长度、初始化 HSE，
   对**运行区**重新计算 SHA-256 并与 NVM 的值比较；一致才允许跳 APP，
   否则必须留在 boot。

**Block3 布局**（`ASW/UserCode/NvmApp.h`，偏移是块内 payload 偏移）

| 偏移 | 长度 | 枚举 | 内容 |
| --- | --- | --- | --- |
| 0 | 32 | `kBlock3_Now_SHAdata` | 当前运行区 APP 的 SHA-256 |
| 32 | 32 | `kBlock3_Pre_SHAdata` | 备份区 APP 的 SHA-256（本期不使用） |
| 64 | 4 | `kBlock3_Now_FileSize` | 当前 APP 的文件长度，小端 |
| 68 | 4 | `kBlock3_Pre_FileSize` | 备份区 APP 的文件长度（本期不使用） |

**改动文件**

| 文件 | 内容 |
| --- | --- |
| `ASW/UserCode/Boot_Program.c/.h` | 新增 `BootProg_StoreImageInfo()`：搬运前写 SHA-256 + 长度并 `Nvm_CommitBlock()`；新增结果码 `BOOT_ST_COPY_NVM`(0x07) |
| `ASW/UserCode/Boot.c` | 新增 `Boot_AppShaVerify()`（NVM+HSE 初始化、读 NVM、哈希运行区、比较）、`Boot_AppShaConfirm()`、诊断数组 `g_bootAppShaDiag[]` |
| `ASW/UserCode/Boot.h` | 声明 `Boot_AppShaConfirm()` 与 HSE 启动重试参数 |
| `MCAL/BSW_Init.c/.h` | 新增幂等的 `BSW_EarlyInitClock()` / `BSW_EarlyInitNvm()`，`BSW_Init()` 复用其结果 |
| `src/main.c` | 改为包含 `BSW_Init.h` |

**`FF02` 的时序（为什么在搬运前写）**

```
FF02 第 1 帧 : 擦除运行区
             -> 成功后才写 NVM（SHA-256 + 长度）并 Commit 到 DFlash
             -> 回答 0x78，继续搬运 8 KB/帧
FF02 后续帧  : 写运行区
最后一帧     : 回读运行区算 SHA-256，与下载摘要比较
             -> 一致：Boot_AppShaConfirm()，允许随后的 10 01 直接跳 APP
```

- 擦除失败时**不写** NVM：老的 APP 还能继续启动；
- 写 NVM 之后才开始改运行区：中途掉电/复位时运行区已经和 NVM 不一致，
  下次启动必然留在 boot，不会跳进半截的镜像。

**启动检查（`Boot_TryJumpToApp`）**

```
1. BSW_EarlyInitClock()        // PLL 160 MHz，HSE / DFlash 需要
2. Boot_HseProbe()             // 只读一次 MU 状态：HSE 就绪了没有？不等
3. BSW_EarlyInitNvm()          // MemAcc -> Fee -> Mem_43_INFLS + RAM 影子
                               // 这段耗时正好当作 HSE 的启动时间（见 9.11）
4. 读 Block3 的 SHA-256、长度（长度必须是 0 < len <= BOOT_APP_SIZE）
5. 第 2 步未就绪时才 Boot_HseWaitReady()：只等剩下的那部分
6. HseHash_Sha256Region(0x00448000, len) 与 NVM 的摘要比较
7. 相等 -> 关中断、Boot_FlashWipe()、Mem_43_INFLS_DeInit()/MemAcc_DeInit()、跳 APP
   否则 -> 直接 return，Boot_AppShaState = BAD
```

- 状态是**粘滞**的：`Boot_AppShaState = BAD` 之后 `Boot_AppIsValid()` 一律返回 0，
  因此 `Boot_Init()` 置 `Boot_Stay = 1`，`Boot_Poll()` 的 5 s 超时和 10 01
  都不会再跳 APP（这是"必须留在 boot"的关键）。
- 检查是**失效关闭**（fail closed）：NVM 没写过、长度非法、HSE 失败、摘要
  不一致都算失败。所以升级到本版本后，必须通过 bootloader 走一次
  `34/36/37 + FF01 + FF02` 才会写入 Block3，之后再上电才能跳 APP。
- `BSW_EarlyInitClock/Nvm` 是幂等的：检查失败后 `main()` 继续调用 `BSW_Init()`
  时不会重复初始化内存栈（`Mem_43_INFLS_Init` 只做一次），避免时钟/等待周期
  被配置两次。
- `0xFF02` 成功后会调用 `Boot_AppShaConfirm()` 把状态置为 OK，所以刷完写
  `10 01` 可以立刻跳 APP，不用再复位一次。

**调试**：`g_bootAppShaDiag[]`（`volatile`，J-Link 直接读）

| 下标 | 含义 |
| --- | --- |
| [0] | 状态：0 = 未检查，1 = 通过，2 = 失败 |
| [1] | NVM 里读到的文件长度 |
| [2] | 失败原因：0x100 块无效 / 0x101 长度非法 / 0x102 HSE 哈希失败 / 0x103 摘要不一致 / 0x104 Nvm_Init 失败 / 0x105 HSE 未就绪 |
| [3] | `HseHash_GetLastError()` |
| [4] | `0x53484132`（"SHA2" 标记） |

**实测注意事项**

1. `0x37`（以及 `FF02` 的最终校验）算的是**已落盘镜像**的 SHA-256，长度取
   `0x34` 报文里的 size，与启动检查用的长度是同一个值；上位机按 `.bin`
   计算的 SHA-256 也应一致。
2. Block3 的写入走 `Nvm_Block3_Write_U8()` + `Nvm_UserBlock_Polling_10ms()`
   （同步等 Fee 作业），与 APP 侧共享同一个 NVM 块；APP 侧目前没有使用
   Block3 的前 72 字节。
3. 上电到 APP 的启动时间会增加；为压低这段开销做了两级优化（staging 8 KB +
   PFlash 直读），见 9.10。

**编译验证**：`Debug_FLASH` 下 `make all` 通过（GCC 10.2，无警告）。

### 9.10 启动哈希提速（staging 8 KB + PFlash 直读）

9.9 的启动校验第一次实测偏慢，原因是 `hse_hash.c` 原来每 2 KB 做一次
`memcpy` + 一次 HSE MU 往返：1.5 MB 镜像 → 750 次往返 + 1.5 MB 内存拷贝。

**改动（`MCAL/HSE/hse_hash.c`）**

| 项目 | 原来 | 现在 |
| --- | --- | --- |
| `HSE_HASH_STAGE_SIZE` | 2 KB | **8 KB**（`s_stage` 位于 `.non_cacheable_data`） |
| PFlash 镜像 | 经 staging 拷贝，2 KB/次 | **HSE 直接读 PFlash**，64 KB/次（`HSE_HASH_DIRECT_CHUNK`） |
| 1.5 MB 镜像的 MU 往返次数 | ≈750 | ≈24，且无 memcpy |
| ITCM/RAM 缓冲区 | 2 KB/次 | 8 KB/次（仍必须走 staging） |

`HseHash_Sha256Region()` 现在先判断区域是否**完全落在 PFlash**
（`0x00400000..0x007FFFFF`）：

```
在 PFlash  -> HseHash_UpdateDirect()：pInput 直接指 PFlash，按 64 KB 分块 UPDATE
否则(ITCM 0x0 / DTCM / SRAM 缓冲) -> 原 staging 路径（8 KB/次）
直接请求被 HSE 拒绝 -> 自动回退 staging 路径（重新 START，结果仍然正确）
```

- PFlash 是只读的，不存在脏 cache line，HSE 直接读物理内存与 CPU 看到的内容
  一致，因此直读路径不需要 cache 维护；
- ITCM（地址 0 的 RAM 刷写驱动）和栈上的缓冲区 HSE 访问不到，仍走 staging；
- 64 KB 是 64 的整数倍，满足 SHA-256 流式 UPDATE 的长度约束，也能保证每块
  的起始地址对齐；
- 若某颗料/某版 HSE 固件拒绝直读 PFlash，代码会自动回退，日志看
  `g_hseHashDiag[2]`（先出现错误码，随后被 staging 路径的成功覆盖）。

**RAM 占用**：`.non_cacheable_data` 从 0x20430510 起 `hse_hash.o` 由 0x8a0
增到 0x20a0（+6 KB），整个非缓存区用到 0x20432b40；该 MPU 区域是 64 KB
（`0x20430000..0x2043FF00`），还剩约 53 KB，无需改链接脚本。
`data` 段相应 +6144 B（staging 的初值在 Flash 里）。

**可调项**

- 还嫌 MU 往返多：把 `HSE_HASH_DIRECT_CHUNK` 调到 256 KB（≈6 次往返），
  代价是单次 HSE 请求变长；
- 想更省 RAM：把 `HSE_HASH_STAGE_SIZE` 调回 2 KB，只影响 ITCM/RAM 数据的哈希。

**编译验证**：`Debug_FLASH` 下 `make all` 通过（GCC 10.2，无警告）；
`text 109 152 / data 14 000 / bss 32 088`。

### 9.11 启动顺序：HSE 等待与 NVM 初始化重叠

HSE 上电后由它自己的固件启动，和 M7 是**并行**的；M7 这边只要在发第一条
HASH 请求之前它准备好就行。原来是「先 Nvm_Init，再 HseHash_Init 死等」，
等于把两段串行时间加起来；现在改成：

```
BSW_EarlyInitClock()          // ~1~2 ms（FXOSC + PLL）
Boot_HseProbe()               // 一次 MU 状态读，几十 ns，不等待
BSW_EarlyInitNvm()            // ~数 ms（Fee 扫描 + 4 个块读入影子）
                              //   HSE 在这段时间里继续启动，零成本
Boot_HseWaitReady()           // 只有 probe 失败才进来，且只等"剩余部分"
```

效果：

- HSE 在 NVM 初始化期间已经就绪时，delay 循环**一次都不执行**（常见情况）；
- 只有上电复位、HSE 启动慢于 NVM 初始化时才会补等，预算 200 × 0.12 ms
  ≈ 25 ms（`BOOT_HSE_INIT_RETRIES` / `BOOT_HSE_INIT_DELAY`），已覆盖
  HSE 固件的正常启动时间；
- `Boot_HseProbe()` 只是 `HseHash_Init()`（读 MU FSR），没有副作用。

注意：单核上没法让两个**阻塞**调用真正并行，所以这里利用的是"HSE 启动是硬件
并行、M7 的空转是纯浪费"这一点。NVM 初始化本身（Fee 内部扫描）仍然串行，
如果以后还想再快，只能从 Fee 扫描或时钟初始化上下手，不建议在启动路径上
裁剪（`10 01` 后 APP 侧还要用同一个 NVM 影子）。

**各阶段实测（先量再优化）**

`Boot_AppShaVerify()` 用 Cortex-M7 的 DWT 周期计数器（纯内核寄存器，不需要
任何驱动/定时器）记录每一阶段，结果放在 `g_bootAppShaTime[]`，J-Link 直接读：

| 下标 | 阶段 | 说明 |
| --- | --- | --- |
| [0] | 时钟初始化 | `BSW_EarlyInitClock()`（此刻还在 48 MHz，周期数偏大是正常的） |
| [1] | HSE 探测 | 一次 MU 状态读 |
| [2] | NVM 初始化 | `BSW_EarlyInitNvm()`（Fee 扫描 + 4 个块读入影子） |
| [3] | 读 Block3 | 32 B + 4 B（RAM 影子，µs 级） |
| [4] | HSE 补等 | 通常为 0 |
| [5] | SHA-256 + 比较 | HSE 直读 PFlash |
| [6] | 合计 | 从进入校验到通过的总周期数 |
| [7] | `0x54494D45` | "TIME" 标记，确认结构体没错位 |

换算：PLL 起来后 160 MHz，**160 个周期 = 1 µs**。例如 `[5] = 2 400 000`
就是 15 ms，`[6] = 4 800 000` 就是 30 ms。

若 `[5]` 仍然很大，说明直读被拒（看 `g_hseHashDiag[4]` 的 UPDATE 次数：
≈24 是直读生效，≈750 是退回 staging）；若 `[2]` 很大，则是 Fee 扫描占主导，
可考虑把 `NVM_BLOCK_ALWAYS_AVAILABLE` / 子区回收等 Fee 特性关掉再试。

### 9.12 APP 启动两阶段 + NVM 切片初始化（目标：150 ms 内出首帧）

需求：上电后 **150 ms 内**必须发出第一帧 CAN。原来 APP 的 `BSW_Init()` 一次性
做完时钟/Port/Platform/GPT/CAN/UART/NVM/Nm/ICU/WDG，其中 `Nvm_Init()` 一项就
50 ms，实测整机 360 ms 才出首帧。

**两阶段拆分（`MCAL/BSW_Init.c/.h`）**

| 阶段 | 内容 | 调用点 |
| --- | --- | --- |
| `BSW_InitEarly()` | MCU 时钟、Port、Platform(中断)、GPT(1 ms tick)、CAN/CanIf | `main()`，OS 启动前 |
| `ASW_Init()` | CAN 表、CanTp、DCM、NM 模型、ASP/DSC、任务注册 | `main()` |
| `BSW_CAN_SendFirstFrame()` | 首帧（`ASW_CanInit()` 已把各 TX 项置为"立即到期"） | `main()`，OS 启动前 |
| `BSW_InitLateTask()` | 看门狗 → ICU → NVM(切片) → UART/DMA | 1 ms、优先级 254 的任务 |

- 首帧时序：`ASW_CanInit()` 把 `last_send_tick` 置成 `0xFFFF0000`，所以第一次
  poll 立刻发；发送成功时记录 `now`，**下一帧正好一个周期后到期**，不会因为
  后面的初始化补发/突发。
- `AutoSarNm_initialize()` 留在 `ASW_Init()`（纯数据初始化，且必须在 `Icu_Init()`
  之前取唤醒原因），ICU 初始化放在延后阶段。
- 依赖延后初始化的任务用 `BSW_IsInitDone()` 门控：2 ms 任务的 UART 轮询、
  1 s 任务的喂狗与 UART 发送。HSE TRNG 自检移到 1 s 任务做一次（纯诊断）。
- NVM 在首帧之后才就绪（约再 60~80 ms），期间读 NVM 的 DID 返回"无数据"。

**NVM 切片（`BSW/src/Nvm.c`）**

新增 `Nvm_InitStep()`：把 `MemAcc_Init + Fee_Init + 内部扫描 + 4 个块读` 做成
状态机，每次调用最多跑 **150 µs**（用 Cortex-M7 的 DWT 周期计数器做时间预算，
不依赖"单次 poll 多贵"），返回 `NVM_INIT_STEP_BUSY/DONE/ERROR`。原来的
`Nvm_Init()` 保留为"循环调用 step"的阻塞封装，boot 侧行为不变。

```
BSW_InitLateTask() 每 1 ms 调一次 Nvm_InitStep()
  -> 单次 ≤150 µs，2 ms / 5 ms 的 CAN 任务照常被调度
  -> Fee 扫描的总工作量不变，约 50 ms 分散在 ~60~80 ms 内完成
```

**任务表改造（`MCAL/UserOS.c/.h`）**

| 接口 | 行为 |
| --- | --- |
| `BSW_RegTask()` | **依次扫描空槽**（`fun == NULL && CycleTime == 0`）分配，不再用 `OS_numTolTaskNum`；返回句柄 0..7，失败返回 `BSW_TASK_HANDLE_INVALID`(255)。任务上下文调用 |
| `BSW_UnregTask(handle)` | 把 `CycleTime` 置 0（tick ISR 不再请求）、清掉待处理就绪字节、从就绪堆中摘除（O(log N)）、`fun = NULL`、优先级置 255，槽位可被后续 `BSW_RegTask` 复用。任务上下文调用 |
| `BSW_RequestTask(handle)` | 事件任务（`CycleTime == 0`）的唯一入口，也可从 ISR 提前唤醒周期任务；只写两个字节，任务/中断上下文均可 |
| `BSW_IsrPit0Ch0()` | 固定遍历 `MAX_TASK_NUM` 个槽，`CycleTime == 0` 的槽跳过；到期任务只置一个就绪字节（O(1)，**不入堆、不关中断**） |
| `BSW_OsStart()` | 有请求时把就绪字节收进堆（每个 O(log N)），再弹出堆顶（最高优先级）运行 |

- **中断侧与调度侧分离**：ISR 只写 `OS_ReadyFlag[handle] = 1` 和 `OS_ReadyAny = 1`，
  堆（`OS_heap` / `OS_heapPos`）只由调度器（线程态）修改，所以 **OS 内部没有任何
  `cpsid i` / `cpsie i`**，不会延迟中断，也不存在边沿事件被合并的问题。
- **不丢请求**：先写 flag 再写 marker；调度器先清 marker 再扫 flag。因此请求要么被
  本次扫描看到，要么让 marker 保持为 1，下一次扫描一定看到。
- 就绪队列是**数组实现的二叉最小堆**（无 malloc）：键为 `Priority`，同优先级按句柄
  升序——与原"从 0 开始扫描、严格小于才替换"的选择顺序完全一致，任务执行顺序不变。
  一个任务最多在堆中挂起一次（`OS_heapPos` 去重），重复请求合并为一次执行。
- `BSW_RegTask` 按 `fun → Priority → LastTime → CycleTime` 的顺序写（`CycleTime` 最后，
  tick ISR 不会看到半初始化的槽位）；`BSW_RegTask/UnregTask` 会改堆，只能在任务
  上下文（上电初始化或任务函数里）调用，`BSW_RequestTask` 无此限制。
- 就绪队列实现可在编译期选择（`UserOS.h` 的 `BSW_OS_PRIO_QUEUE_ENABLE`）：
  `1`（默认）为上面的二叉最小堆；`0` 回到原来的纯轮询实现——ISR 只置 `Request`
  字节，调度器每轮扫描全部槽。两者 API、优先级规则和执行顺序完全一致；N=8 时
  轮询版约省 370 B Flash、26 B RAM。
- `MAX_TASK_NUM` 默认 8，可用 `-DMAX_TASK_NUM=...` 覆盖（上限 255，句柄为 8 位）。
- 延后初始化做完后**注销自己**：`BSW_InitLateTask()` 在完成最后一步时调用
  `BSW_UnregTask(BSW_LateTaskHandle)`（调度器调用它之前已把它弹出就绪堆、清掉就绪
  字节，且 `CycleTime = 0` 不会再被请求，任务内自注销是安全的），槽位可被后续任务复用。
- 若注册失败（槽位满），`BSW_InitLateStart()` 自动退回
  `BSW_InitLateBlocking()`，保证 NVM/UART/看门狗仍可用。

**实测预期**

| 阶段 | 之前 | 现在 |
| --- | --- | --- |
| Bootloader（时钟 3.4 + NVM 50 + 哈希 1.5） | ~55 ms | ~55 ms（未压缩） |
| APP 到首帧（时钟/Port/Platform/GPT/CAN + ASW_Init + 首帧） | 计入 305 ms | ~7~11 ms |
| **上电→首帧** | **360 ms** | **~62~66 ms** + 板级复位释放时间 |

**编译验证**：`S32K344/Debug_FLASH` 下 `make all` 通过（GCC 10.2，无警告）；
`text 88 204 / data 2 212 / bss 25 400`。

### 9.13 未初始化访问的拦截（BSW 层）

延后初始化带来一个新的窗口：OS 已经跑起来（首帧已发），但 NVM/UART/DMA/
看门狗还在后台初始化（约 60~80 ms）。这段窗口里任何"用到这些模块"的调用都
必须在 **BSW 层**被拦住，而不是靠调用方自觉。逐个模块核查结果：

| 模块 | 窗口内可能被调用的地方 | 拦截方式 |
| --- | --- | --- |
| NVM 写/提交 | `2E F190`、`Nvm_UserBlock_Polling_10ms()`、NM 睡眠路径 `Nvm_Flush()` | `Nvm.c` 新增 `Nvm_RejectIfNotReady()`：`Nvm_WriteByte` / `CommitBlock` / `Commit` / `StoreBlock` / `MarkBlockDirty` / `InvalidateBlock` / `Flush` / `MainFunction` 未就绪一律 `E_NOT_OK`（或直接返回） |
| NVM 读 | `22 F190`、其它读块的 DID | `Nvm_Valid[]` 未就绪时为 FALSE → `Nvm_ReadByte()` 返回 `E_NOT_OK`；读出 0xFF 的"未写入"语义由上层区分 |
| NVM 初始化自身 | `Nvm_InitStep()` 里格式化块后的写 | 走内部 `Nvm_CommitBlockRaw()`，不受拦截 |
| UART/DMA | 2 ms 任务 `BSW_UART6_RxPoll()`、1 s 任务 `BSW_UART6_Send()` | `BSW_UART.c` 新增 `BSW_UART6_RejectIfNotReady()`：未就绪时轮询直接返回、发送返回 `E_NOT_OK`、`IsTxBusy()` 返回 FALSE、`GetRxPending()` 返回 0；`BSW_UART6_Init()` 末尾才置 ready |
| 看门狗 | 1 s 任务喂狗 | 新增 `BSW_WDG.c/.h`：`BSW_WDG_Service()` 未就绪时计数并跳过，`BSW_WDG_Init()` 才真正初始化驱动 |
| ICU | 仅 NM 睡眠路径 `Icu_EnableEdgeDetection()` | 已核实该路径需要 NM 多个超时（秒级）才会走到，晚于初始化完成；`NM_BsmGetWkupMode()` 是**故意**在 `Icu_Init()` 之前读 WKPU 裸寄存器，不依赖 Icu 驱动 |
| DMA | 只在 `MCAL_UART_Init()` 内部使用 | 与 UART 同生命周期，由 UART 的拦截覆盖 |

**语义变化（都是"更正确"的方向）**

- `22 F190` 在 NVM 未就绪时返回 NRC 0x22（原来会返回 17 个 0xFF，看起来像
  "VIN 未写入"）；
- `2E F190` 在 NVM 未就绪时返回 NRC 0x22；
- UART 发送在驱动未就绪时返回 `E_NOT_OK`（与"上一帧未发完"同一个返回码，
  调用方本来就要处理）；
- 喂狗在驱动未就绪时不动作，等 `BSW_WDG_Init()` 之后才真正喂。

**诊断计数**（J-Link 直接读，可用来证明拦截生效）

| 变量 | 含义 | 正常上电时的值 |
| --- | --- | --- |
| `g_nvmNotReadyRejects` | NVM 未就绪被拒绝的调用数 | 只有窗口内有 DID 访问时 >0 |
| `g_uartNotReadyRejects` | UART 未就绪被拒绝的调用数 | ≈40（2 ms 任务轮询 80 ms） |
| `g_wdgNotReadyRejects` | 看门狗未就绪被跳过的喂狗次数 | 0（1 s 任务晚于初始化完成） |

由于 BSW 层已经是权威拦截，ASW 里原来的 `BSW_IsInitDone()` 门控（2 ms 的
UART 轮询、1 s 的喂狗/UART 发送）已去掉，避免两处重复判断；`BSW_IsInitDone()`
保留给需要的应用层使用。

### 9.14 AB Swap：用 HSE 分区切换替换 0xFF02 搬运

**背景**：本工程安装的是 `HSE_FW_S32K344_0_2_40_4`，该版本**只支持 AB Swap**
配置（HSE 固件包目录为 `hse_ab_swap`）。AB Swap 下 4 MB 代码 Flash 被 HSE 均分
为两个 2 MB 分区，M7 永远从 active 分区地址运行，被动分区只能编程、不能执行。
调用 `HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK` 后，下一次复位时 HSE 交换
active/passive 分区。

| 区域 | active 地址 | passive 地址 |
| --- | --- | --- |
| BOOT | `0x00400000`（256 KB） | `0x00600000` |
| BOOT 标志扇区 | `0x00440000` | `0x00640000` |
| APP | `0x00448000`（`0x18C000`） | `0x00648000` |
| HSE FW（保留） | `0x005D4000..0x005FFFFF` | `0x007D4000..0x007FFFFF` |

**核心改动**

| 文件 | 内容 |
| --- | --- |
| `ASW/UserCode/Boot.h` | 新增 passive 地址宏 `BOOT_CODE_BACKUP_BASE` / `BOOT_APP_BACKUP_BASE`、`BOOT_PASSIVE_OFFSET`；HSE 等待预算从 200×0.12 ms 提高到 20000×0.12 ms（切区复位后 HSE 要把自身固件备份到被动区，约 1 s 才置 `HSE_STATUS_INIT_OK`） |
| `ASW/UserCode/Boot_Program.c/.h` | 地址映射/擦除范围改为只允许 passive 分区；BOOT/APP 两个镜像的 SHA-256 分开跟踪；`BootProg_Block3ShiftPair()` / `BootProg_Block3PrepareSwap()` / `BootProg_Block3StateRead/Write()`（两对镜像成对 Shift 或交换，失败回滚）；编程计数器 `BootProg_CountProgramSuccess()` / `BootProg_CountProgramAttempt()`；`BootProg_SwapStep()`（0xFF02）；`BootProg_AbSwap()`（0xACC1）；`BootProg_ActivatePassiveBlock()` |
| `ASW/UserCode/Boot.c` | `Boot_ImageHeaderValid(imageBase, runBase, size)`：被动区镜像的向量表在 `imageBase + (vector - runBase)`，不能直接用绝对地址；`Boot_PassiveBootValid()` 现在还会**回读被动区 BOOT 的 SHA-256 与 `kBlock3_PreBootSHAdata` 比较**（诊断 `g_bootPassiveBootDiag[4]`）；`Boot_PassiveAppValid()` |
| `MCAL/HSE/hse_host.c/.h` | 新增 `HSE_ActivatePassiveBlock()`，服务描述符放在 `.non_cacheable_data`（HSE 需要能读到，且切区相关变量不能放在普通可缓存 RAM） |
| `UDS/UDS_DcmCfg.c` | 0xFF02 改调 `BootProg_SwapStep()`；新增 `31 01 AC C1` 例程；成功后置 `ResetPending`，正响应发出后自动复位 |

**地址映射**：上位机仍按镜像的**运行地址**下发，Boot 内部映射到 passive：

```
0x00400000 -> 0x00600000   (BOOT)
0x00440000 -> 0x00640000   (标志扇区)
0x00448000 -> 0x00648000   (APP)
```

直接下发 `0x00600000` / `0x00648000` 也可以。`0x005D4000..0x005FFFFF` 与
`0x007D4000..0x007FFFFF`（HSE FW 区）永远不允许擦写；active 分区也不允许擦写
（Boot 自己就在 active 分区里执行）。

**0xFF02（APP 升级 + 切区）**

```
34/36/37  flash driver -> ITCM
34/36/37  BOOT (0x400000 -> 0x600000，可选)
34/36/37  APP  (0x448000 -> 0x648000)
31 01 FF 01   依赖校验：APP 版本（正向升级；可用 2E F7F7 强制）+ APP/BOOT
              被动区头 + 回读 SHA-256
31 01 FF 02   1. 被动区 APP 头/向量表检查
              2. 回读被动区 APP SHA-256，与 0x37 摘要比较
              3. 若本次下载了 BOOT，回读被动区 BOOT SHA-256 比较
              4. 本次没下 BOOT 时，被动区 BOOT 必须与 Block3
                 kBlock3_PreBootSHAdata 一致（头 + 整个镜像 SHA-256，
                 没有有效 BOOT 就拒绝切区，避免变砖）
              5. Block3 两对镜像都 Shift（同步 Commit）：
                 APP ：Pre <- old Now，Now <- 新 APP SHA/长度
                 BOOT：Pre <- old Now，Now <- 新 BOOT SHA/长度（本次没下
                 BOOT 时这一对是"交换" Pre <- Now、Now <- old Pre）
              6. HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK
              7. 成功则 kBlock3_program_cnt++（Commit 后再算）
              8. 正响应 71 01 FF 02 00 发出后 100 ms 自动复位
复位后 HSE 交换分区，新 BOOT/APP 在 0x400000/0x448000 运行
```

**0xFF02（只刷 BOOT，和 APP 走同一套流程）**

```
34/36/37  flash driver
34/36/37  BOOT (0x400000 -> 0x600000)
31 01 FF 01   BOOT 被动区头 + 回读 SHA 比较；
              被动区 APP 仍按 Block3 Pre 校验（APP 本次不更新，跳过版本策略）
31 01 FF 02   1. 被动区 BOOT 头/SHA 检查（用本次 0x37 的摘要）
              2. 被动区 APP 与 Block3 Pre 一致
              3. Block3 两对镜像都交换：Pre <- old Now，Now <- old Pre
                 （换区后运行的是被动区原有 APP/BOOT，与 Pre 对应；
                  本次下载的新 BOOT 已由第 1 步验证，它就是新的 Now）
              4. HSE 切区；成功则 kBlock3_program_cnt++，正响应后自动复位
复位后新 BOOT + 被动区原 APP 运行
```

> 只刷 BOOT 时 APP 会随分区一起切到另一区的旧/当前版本，这是允许的。
> 两个分区都更新到新 BOOT 需要**人工刷两遍**：第一遍更新被动区 BOOT 并切区，
> 第二遍把 BOOT 刷到另一个（现在是 passive 的）区再切一次。

不再擦除/搬运 active 分区，所以 FF02 失败时旧 APP 仍然完整可启动。

**0x31 01 AC C1（回滚：不上传任何新镜像）**

```
31 01 AC C1
    1. 读 Block3 Pre SHA/长度；无效则回 0x0A
    2. 被动区 APP 头 + 回读 SHA-256 必须与 Pre 一致（否则 0x0B）
    3. 被动区 BOOT：头 + 回读 SHA-256 必须与 kBlock3_PreBootSHAdata
       一致（否则 0x05）
    4. Block3 两对镜像都交换：Pre <- old Now，Now <- old Pre
    5. HSE 切区；正响应后自动复位（ACC1 不计数，因为没有编程动作）
```

**0xFF02 / 0x31 01 AC C1 的 status（`71 <subFn> <RID> <status>`）**

| status | 含义 | 常见原因 |
| --- | --- | --- |
| `0x00` | 成功，已请求 HSE 切区 | 正响应后自动复位 |
| `0x01` | 未通过 0xFF01 | 先执行 `31 01 FF 01` |
| `0x02` | 下载信息不满足 / 重复切区 | 既没下 APP 也没下 BOOT、已请求过切区 |
| `0x03` | 被动区 APP 头/向量表非法 | APP 没下完整、地址映射错 |
| `0x04` | 被动区 APP SHA-256 不一致 | 回读的镜像与 0x37 摘要不同 |
| `0x05` | 被动区 BOOT 镜像无效 | 先把 BOOT 刷到 `0x00600000` |
| `0x06` | 新 BOOT SHA-256 不一致 | BOOT 下载/擦除不完整 |
| `0x07` | Block3 SHA/长度写入失败 | Fee/DFlash 提交失败 |
| `0x08` | HSE 切区服务失败 | HSE 不是 AB Swap 固件/HSE 忙 |
| `0x09` | HSE SHA-256 计算失败 | HSE 未就绪 / 地址非法 |
| `0x0A` | ACC1：Block3 Pre 无效 | 还没通过 FF02 建立被动区 SHA |
| `0x0B` | ACC1：被动区 APP 与 Pre 不一致 | 被动区 APP 被改过/下载中断 |

FF01（`71 01 FF 01 <status>`）在只刷 BOOT 时新增：`0x17` = Block3 Pre 无效或
被动区 APP 与 Pre 不一致（需要先跑一次完整的 APP 升级建立 Pre）。

**Block3 不变量**（`ASW/UserCode/NvmApp.h`，payload 偏移，全部小端）

| 偏移 | 长度 | 枚举 | 内容 |
| --- | --- | --- | --- |
| 0 | 32 | `kBlock3_Now_SHAdata` | 当前 active APP 的 SHA-256 |
| 32 | 32 | `kBlock3_Pre_SHAdata` | passive APP 的 SHA-256 |
| 64 | 4 | `kBlock3_Now_FileSize` | active APP 长度 |
| 68 | 4 | `kBlock3_Pre_FileSize` | passive APP 长度 |
| 72 | 32 | `kBlock3_NowBootSHAdata` | 当前 active BOOT 的 SHA-256 |
| 104 | 32 | `kBlock3_PreBootSHAdata` | passive BOOT 的 SHA-256 |
| 136 | 4 | `kBlock3_NowBootSize` | active BOOT 长度 |
| 140 | 4 | `kBlock3_PreBootSize` | passive BOOT 长度 |
| 144 | 4 | `kBlock3_program_cnt` | 编程计数器（0xFF02 成功 +1） |
| 148 | 4 | `kBlock3_program_retry_cnt` | 尝试编程计数器（APP 0x34 +1） |

- `Now` 永远描述"切区后正在运行"的镜像，`Pre` 描述另一个分区里的镜像；
  `Boot_AppShaVerify()` 只用 `Now` 校验运行中的 APP；
  `Boot_PassiveBootValid()` 用 `PreBoot` 校验被动区 BOOT。
- 0xFF02（本次下了该镜像）：`Pre <- Now`，`Now <- 新下载`；
- 0xFF02（本次没下该镜像）/ 0xACC1：`Pre <- Now`，`Now <- old Pre`（交换）；
- **APP 和 BOOT 两对都按上面的规则处理**，由
  `BootProg_Block3PrepareSwap()` 一次完成，`Nvm_UserBlock_Polling_10ms()`
  同步 Commit；HSE 拒绝切区或写入失败时用 `BootProg_Block3StateWrite(旧快照)`
  整体恢复，保证旧镜像仍能通过启动校验。
- 旧固件（只有 APP 那 4 项）升级上来时，BOOT 四项是 0xFF：第一次升级必须
  同时下发 BOOT+APP，切区后四项就补齐了。

**编程计数器的写入时机**

| 计数器 | 何时 +1 | 说明 |
| --- | --- | --- |
| `kBlock3_program_cnt` | 0xFF02 的 HSE 切区被接受之后 | APP 升级、只刷 BOOT 都算；0xACC1 不算（没有编程） |
| `kBlock3_program_retry_cnt` | `BootProg_RequestDownload()` 接受 APP 的 0x34 时 | 每次 APP 编程尝试 +1，与是否最终成功无关 |

两者都是 4 字节小端、读-改-写后立即 Commit；从未写过的值读作 `0xFFFFFFFF`，
按 0 处理（第一次 +1 得到 1）。

**新板子产线流程（两个区各刷一次 BOOT，最后只刷一次 APP）**

场景：新板子用 J-Link 往 active 区（`0x400000`）烧第一个 BOOT，之后用刷写流程
把 BOOT 依次写进两个分区，最后把 APP 刷一次。每一步的 Block3 状态：

| STEP | 报文 | Block3 关键项 | 备注 |
| --- | --- | --- | --- |
| 1 | J-Link 烧 `0x400000` 的 BOOT | 全 0xFF | 无 APP → 停在 BOOT |
| 2 | `34/36/37` BOOT → `0x600000`；`FF01`；`FF02` | `NowBoot={D_boot,S1}`，`PreBoot={D_win1,256K}`，`cnt=1` | `FF01` 走"新板子无 APP"分支；切到第二区 |
| 3 | `34/36/37` BOOT → `0x600000`；`FF01`；`FF02` | `NowBoot=PreBoot={D_boot,S1}`，`cnt=2` | 两区都是同一个 BOOT；切回第一区 |
| 4 | `34/36/37` APP → `0x648000`；`FF01`；`FF02` | `NowApp={D_app,S_app}`，`PreApp=0xFF`，`cnt=3`，`retry=1` | 首次编程（active 区无 APP）跳过版本策略；切到第一区后 APP 运行 |

此时两个分区都有 BOOT，**只有 active 区有 APP**（产线只刷一次 APP）。

> 如果之后想把同一个 APP 也写进另一个分区：因为 `FF01` 的正向升级策略要求
> **严格升级**，同级版本会返回 `0x06`，先发 `2E F7 F7 01` 强制升级即可
> （`FF02` 随后照常把两区 APP/SHA 对齐）。

关键判定（新板子专用，不影响正常产品升级）：

1. `BootProg_NoAppProgrammed()`：Block3 可读且 `Now_FileSize` 与 `Pre_FileSize`
   都还是 `0xFFFFFFFF` → 认定"从未编程过 APP"，BOOT-only 的 `0xFF01` / `0xFF02`
   跳过"被动区 APP 必须与 PreApp 一致"的检查（此时没有 APP 需要保护）；
2. `check_version()`：active 区 APP 镜像头非法（空区）→ **"首次编程"**，
   不做版本比较（否则新板子会返回 `0x04` VER_NOW）；
   其余情况仍按"只允许严格升级"：降级和同级都返回 `0x06`，
   可用 `2E F7 F7 01` 强制；
3. `BootProg_BootIdentity()`：某个 BOOT 的 SHA/长度从未记录（新板子 active 区
   的 BOOT 是 J-Link 烧的）时，**把整个 BOOT 窗口（256 KB）按现状做 SHA-256**
   作为它的身份，长度记 `BOOT_CODE_SIZE`。这样切区后：
   - `NowBoot` = 切区后运行的（今天被动区的）BOOT；
   - `PreBoot` = 切区后被动区的（今天 active 的）BOOT；
   之后 `Boot_PassiveBootValid()` 用同样的 256 KB 窗口再算一次必然一致，
   所以 STEP4 的 APP 切区能通过；下一次正常升级（BOOT+APP）后自动回落到
   "镜像实际长度"的记录方式。


> 注意：STEP2/3 用的是"只刷 BOOT"路径，前提是本次**下载了 BOOT**
> （`g_bootImage.Valid`），被动区 BOOT 用 0x37 摘要校验；STEP4 的
> `0xFF01` 里 `g_bootImage.Valid = 0`，被动区 BOOT 用 Block3 的 `PreBoot`
> 校验（即 STEP3 写入的摘要）。

**首次迁移到 AB Swap 的注意事项**

1. 现网已量产板（旧固件、Block3 只有 APP 四项）升级时，第一次仍建议同时下发
   BOOT+APP：虽然新代码允许"只刷 BOOT"，但旧板子两个分区的 BOOT 都不支持
   AB Swap 时切区后无法再切回（见第 4 条）；
2. 版本策略仍是"只允许严格升级"：如果当前 APP 与升级包版本相同（含把同一个
   APP 写进第二个分区），`FF01` 返回 `0x06`，先发 `2E F7 F7 01` 强制升级；
3. 切区复位后第一次启动，HSE 约 1 s 才就绪；`Boot_AppShaVerify()` 会等到
   `HSE_STATUS_INIT_OK` 再算 SHA-256，属正常现象；
4. 两个分区里的 BOOT 都必须是支持 AB Swap 的版本，否则切到旧 BOOT 后无法再切回。

**切区复位后的时钟顺序**：HSE 在切区复位期间要把自身固件备份到新的被动区，
这 ~1 s 内不能改 `HSE_CLK`（RTD 的 `Clock_Ip_Divider.c` 会等
`PRTN0_CORE2_STAT.WFI`，超时 50 ms 就会跳过该分频器）。因此
`BootProg_ActivatePassiveBlock()` 在 HSE 接受切区后把
`0x20443FF8 = 0x5A5AAB5A`（`BOOT_RAM_AB_SWAP_MAGIC`，与 0x20443FF0 的
APP→Boot 握手共用同一个复位保持区）；新的 BOOT 在 `Boot_TryJumpToApp()` 里
**先** `HseHash_WaitReady()` 等 HSE 就绪、清标志，**再**做 `BSW_EarlyInitClock()`，
避免在 HSE 忙时配置 HSE_CLK。

**APP 读取当前运行分区：`22 AB CC`**

```
22 AB CC          -> 62 AB CC <bank>
  bank 0x41 ('A') = A 区（低地址：BOOT 0x00400000 / APP 0x00448000）
  bank 0x42 ('B') = B 区（高地址：BOOT 0x00600000 / APP 0x00648000）
  bank 0xFF = 无法判断（DCM 扫描未完成，或本机不是 AB Swap 配置）
```

实现：`UDS_DcmCfg.c` 里的 `ASW_GetActiveBank()` 直接读
DCM 状态寄存器 **DCMSTAT = 0x402A_C000**：

| 位 | 字段 | 用法 |
| --- | --- | --- |
| 0 | `DCMDONE` | 必须为 1，后面的字段才有效 |
| 16 | `DCMOTAA` | `1` = AB Swap/OTA 已启用；`0` 返回 `0xFF` |
| 17 | `DCMOTAR` | `0` = 低地址（A 区）；`1` = 高地址（B 区） |

> 注意：NXP 例程 `hse_dcm_register.c` 的 `Dcm_ActiveAddressOTARegion()` 把
> `DCMOTAR` 的 0/1 映射反了，NXP 社区已确认以 AN13465 的
> “0 = Low address / 1 = High address” 为准（代码里已按 AN13465 实现）。
> 该寄存器在功能复位后由 DCM 重新扫描，所以切区复位后 APP 立刻能读到新分区。

**诊断**：J-Link 读 `g_bootAbSwapDiag[4]`

| 下标 | 含义 |
| --- | --- |
| [0] | 最后一次 0xFF02 / 0xACC1 的 status（`BOOT_ST_AB_xxx`） |
| [1] | HSE 原始响应（`0x55A5AA33` = OK；`0xAA55A11E` = 不支持，说明装的不是 AB Swap 固件） |
| [2] | 1 = 0xFF02，2 = 0xACC1 |
| [3] | `0x41425357`（"ABSW" 标记） |

**编译验证**：`S32K344_Bootloader/Debug_FLASH` 下 `make all` 通过（GCC 10.2，
无警告）；`text 111 096 / data 14 208 / bss 23 816`，仍远小于 256 KB BOOT 区。



