@ECHO OFF
::del .\CHERY_E0V_NEW_600E5*.s19
::
:: 从 APP / BOOT 的 S19 镜像中读取固定信息块，复制到当前文件夹并重命名：
::   0x100 偏移  软件版本号（如 1.1.0）
::   0x160 偏移  编译日期（__DATE__，如 Nov 11 2026）
::   0x170 偏移  编译时间（__TIME__，如 14:02:11）
:: 文件名格式：<名称>_<软件版本>_<ver>_<yyyyMMdd>_<HHmmss>.s19
:: 日期和时间取自镜像内的编译信息，不再使用复制时的系统日期和时间。

setlocal enabledelayedexpansion

:: 以脚本所在目录为当前文件夹
cd /d "%~dp0"

del .\*.srec
del .\*.s19

set "ver=V1"

:: APP 镜像（基地址 0x00448000）
set "app_file=.\S32K344\Debug_FLASH\S32K344.s19"
set "app_name=S32K344_App"
set "app_base=00448000"

:: BOOT 镜像（基地址 0x00400000）
set "boot_file=.\S32K344_Bootloader\Debug_FLASH\S32K344_Bootloader.s19"
set "boot_name=S32K344_Boot"
set "boot_base=00400000"

call :Rename_File "%app_file%" "%app_name%" "%app_base%"
call :Rename_File "%boot_file%" "%boot_name%" "%boot_base%"

exit /b

:Rename_File
set "file=%~1"
set "rename=%~2"
set "base=%~3"

if not exist "%file%" (
    echo 未找到 %file% 对应的原文件，跳过该组处理
    goto :eof
)

set "src=%file%"
set "img_base=%base%"
set "sw_ver="
set "sw_date="
set "sw_time="

for /f "usebackq tokens=1,* delims==" %%a in (`powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try{ $f=$env:SRC; $b=[Convert]::ToUInt32($env:IMG_BASE,16); $m=@{}; $lines=[System.IO.File]::ReadAllLines((Resolve-Path -LiteralPath $f).Path); foreach($l in $lines){ $l=$l.Trim(); if($l.Length -lt 12){continue}; $t=$l.Substring(0,2).ToUpper(); if($t -eq 'S1'){$al=2}elseif($t -eq 'S2'){$al=3}elseif($t -eq 'S3'){$al=4}else{continue}; $cnt=[Convert]::ToInt32($l.Substring(2,2),16); $a=[Convert]::ToUInt32($l.Substring(4,2*$al),16); $n=$cnt-$al-1; for($i=0;$i -lt $n;$i++){ $m[[int64]($a+$i)]=[Convert]::ToByte($l.Substring(4+2*$al+2*$i,2),16) } }; $val=@(); foreach($ad in @(($b+0x100),($b+0x160),($b+0x170))){ $ba=New-Object byte[] 16; for($i=0;$i -lt 16;$i++){ $v=$m[[int64]($ad+$i)]; if($null -eq $v){$v=0}; $ba[$i]=[byte]$v }; $val+=([System.Text.Encoding]::ASCII.GetString($ba)).Trim([char]0).Trim() }; $sv=($val[0] -replace '[\\/:*?<>|]','-').Trim(); if([string]::IsNullOrWhiteSpace($sv)){throw 'version empty'}; $ci=[System.Globalization.CultureInfo]::InvariantCulture; $d=[datetime]::Parse(($val[1] -replace '\s+',' '),$ci); $tm=[datetime]::Parse($val[2],$ci); Write-Output ('VER='+$sv); Write-Output ('DATE='+$d.ToString('yyyyMMdd')); Write-Output ('TIME='+$tm.ToString('HHmmss'))}catch{exit 1}"`) do (
    if /i "%%a"=="VER"  set "sw_ver=%%b"
    if /i "%%a"=="DATE" set "sw_date=%%b"
    if /i "%%a"=="TIME" set "sw_time=%%b"
)

if not defined sw_ver (
    echo 未能从 %file% 的 0x100 地址读取软件版本号，跳过该组处理
    goto :eof
)
if not defined sw_date (
    echo 未能从 %file% 的 0x160 地址读取编译日期，跳过该组处理
    goto :eof
)
if not defined sw_time (
    echo 未能从 %file% 的 0x170 地址读取编译时间，跳过该组处理
    goto :eof
)

echo ver res %sw_ver%  build %sw_date% %sw_time%

copy "%file%" ".\%rename%_%sw_ver%_%ver%_%sw_date%_%sw_time%.s19"

goto :eof
