$ErrorActionPreference = "Stop"
$gcc = "D:\NXP\S32DS\build_tools\gcc_v10.2\gcc-10.2-arm32-eabi\bin"
$env:Path = "$gcc;$env:Path"
$d = $PSScriptRoot
$rtdInc = Get-ChildItem "$d\..\S32K344\MCAL\RTD_PACK" -Directory -ErrorAction SilentlyContinue | ForEach-Object { "$($_.FullName)\include" }
$inc = @(
  "$d\src",
  "$d\MCAL\EB_GEN\include",
  "$d\MCAL\RTD_PACK\BaseNXP_TS_T40D34M50I0R0\header",
  "$d\MCAL\RTD_PACK\BaseNXP_TS_T40D34M50I0R0\include",
  "$d\MCAL\RTD_PACK\Mem_43_INFLS_TS_T40D34M50I0R0\include",
  "$d\Project_Settings\Linker_Files"
) + $rtdInc | Select-Object -Unique | ForEach-Object { "-I`"$_`"" }
$defs = @("-DINIT_STDBY_RAM", "-DARMCM7_SP")
$srcs = @(
  "main.c", "C40_Ip.c", "C40_Ip_Ac.c", "C40_Ip_Cfg.c", "Cache_Ip.c",
  "Cache_Ip_HwAcc_ArmCoreMx.c", "Mem_43_INFLS.c", "Mem_43_INFLS_Cfg.c",
  "Mem_43_INFLS_Ipw.c", "Mem_43_INFLS_Software_Semaphore.c",
  "SchM_Mcl.c", "SchM_Mem_43_INFLS.c"
) | ForEach-Object { "$d\src\$_" }
$common = @("-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-d16",
            "-O2", "-g3", "-Wall", "-ffunction-sections", "-fdata-sections") + $defs + $inc
New-Item -ItemType Directory -Force -Path "$d\Debug_FLASH" | Out-Null
$objs = @()
foreach ($s in $srcs) {
  $o = "$d\Debug_FLASH\obj_" + [System.IO.Path]::GetFileNameWithoutExtension($s) + ".o"
  & arm-none-eabi-gcc @common -c $s -o $o
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
  $objs += $o
}
New-Item -ItemType Directory -Force -Path "$d\Debug_FLASH" | Out-Null
& arm-none-eabi-gcc @common -T "$d\Project_Settings\Linker_Files\S32K344_flashdriver_ram.ld" "-Wl,--gc-sections" "-specs=rdimon.specs" -o "$d\Debug_FLASH\S32K344_FlashDriver.elf" @objs
if ($LASTEXITCODE -ne 0) { throw "link failed" }
& arm-none-eabi-objcopy -O binary "$d\Debug_FLASH\S32K344_FlashDriver.elf" "$d\Debug_FLASH\S32K344_FlashDriver.bin"
& arm-none-eabi-size "$d\Debug_FLASH\S32K344_FlashDriver.elf"
Get-ChildItem "$d\Debug_FLASH\S32K344_FlashDriver.bin" | Select-Object Name, Length

