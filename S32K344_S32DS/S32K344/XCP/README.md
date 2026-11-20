# XCP（仅参数标定）

`XCP.c` / `XCP.h` / `XCP_Para.c` / `XCP_Para.h` 组成一个只做参数标定的 XCP 从机，
挂在 CAN 上（经典 CAN，8 字节帧）。

## 1. 支持的指令

只有 5 条，其它指令一律回 `FE 20`（ERR_CMD_UNKNOWN）：

| 指令 | CTO（主机 -> 从机） | DTO（从机 -> 主机） |
| --- | --- | --- |
| CONNECT `0xFF` | `FF 00` | `FF 00 00 08 08 00 01 01` |
| DISCONNECT `0xFE` | `FE` | `FF 00` |
| SET_MTA `0xF6` | `F6 00 00 ext addr[4]`（Intel） | `FF` |
| DOWNLOAD `0xF0` | `F0 n data[n]`（n = 1..6） | `FF` |
| SHORT_UPLOAD `0xF4` | `F4 n 00 ext addr[4]`（Intel，n = 1..7） | `FF data[n]` |

- 应答固定一帧 8 字节，没用的字节补 0。
- 否定应答固定 `FE <错误码>`：`21` 指令长度/参数错误、`22` 地址越界、`25` 未连接。
- `DOWNLOAD` 成功后 MTA 自动后移，可以连续下载。
- `SHORT_UPLOAD` 不改变 MTA，用它读回当前标定值。

## 2. 标定量放在哪

`XCP_Para.c` 里的所有对象都带 `XCP_CAL_DATA` 属性，被放进 `.xcp_cal` 段；
链接脚本（`S32K344_flash.ld` / `S32K344_ram.ld`）把该段放在 `.data_tcm_data` 里，
所以：

- 数据在 **DTCM**（当前链接结果：`0x20000000` .. `0x20000014`，见 `.map`）；
- 上电时由启动代码从 Flash 拷贝，`XCP_Para.c` 里写的初值就是标定初值；
- `KEEP()` 保证即使没有 C 代码引用，标定量也不会被 `--gc-sections` 删掉。

增加一个可标定量：

1. `XCP_Para.c` 中定义：`XCP_CAL_DATA uint16 XCP_Cal_xxx = 默认值;`
2. `XCP_Para.h` 中声明：`extern uint16 XCP_Cal_xxx;`
3. 在 `.map` 里查该变量在 `.xcp_cal` 段的地址，作为 A2L 中标定量的地址。

## 3. 地址保护

只有 `.xcp_cal` 段（位于 DTCM）可以被 XCP 访问：

- `SET_MTA` 的地址不在该范围内 → 回 `FE 22`，同时让 MTA 失效；
- 紧随其后的 `DOWNLOAD` 也会被拒绝（回 `FE 22`），即“拒绝这次标定”；
- `DOWNLOAD` 还检查 `[MTA, MTA+n)` 是否整块在范围内，防止写到边界外；
- `SHORT_UPLOAD` 同样只允许读该范围。

范围比“整个 DTCM”更严格：DTCM 里还有栈和其它数据，不能让标定穿透过去。
链接脚本里有 `ASSERT()`，如果以后有人把 `.xcp_cal` 挪出 DTCM，链接会直接报错。

## 4. 必须补的一条 CAN 配置

FlexCAN 用的是**增强 RX FIFO + 硬件 ID 滤波**，当前滤波表（`Can_43_FLEXCAN_PBcfg.c`
中 `Can_aHwFilter_Object0`）里没有 `0x187A55AA`，XCP 请求会被硬件直接丢掉，
`XCP_CallBack()` 永远不会被调用。请用 S32 Config Tools 在 Can 的接收对象里
给增强 FIFO 增加一条扩展 ID 滤波 `0x187A55AA`（掩码 `0x1FFFFFFF`）后重新生成。

另：`ASW_CanCallback.h` 的 `CAN_CHN1_RX_COUNT` 已从 3 改成 4，RX 表里第 4 项就是
`{XCP_RX_ID, &XCP_REQ, ..., XCP_CallBack}`。

## 5. 传输方式与限制

- 请求在 `BSW_CanRxCallout()`（2ms 任务的 `Can_43_FLEXCAN_MainFunction_Read()`）里进来，
  `XCP_CallBack()` 直接调用 `Can_43_FLEXCAN_Write()` 用 HTH 1 发出应答，
  不需要往周期发送表里加条目。
- 如果发送 buffer 全忙（周期报文正在发），这一帧应答会被丢掉，主机在 T1 超时后重发请求。
- 不支持地址扩展（paging），`ext` 字节被忽略，标定量按 32 位地址寻址。
- 不支持 DAQ/STIM、标定页切换、校验和、种子密钥等其它功能。
- 注意：UDS `0x28 CommunicationControl` 关闭“常规接收”时，会把 RX 表里除诊断和 NM
  之外的条目一起关掉，XCP 也会被关掉。若希望 XCP 始终可收，可在
  `ASW_SetNormalCommEnabled()` 里把 `XCP_RX_ID` 一并排除。
