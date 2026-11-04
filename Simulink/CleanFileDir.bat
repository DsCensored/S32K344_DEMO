@echo off
setlocal enabledelayedexpansion
cd /d %cd% 

if exist %cd%\*.slxc (del %cd%\*.slxc)
if exist %cd%\*.mexw64 (del %cd%\*.mexw64)
if exist %cd%\slprj (rd /s /Q %cd%\slprj)
if exist %cd%\slcov_output (rd /s /Q %cd%\slcov_output)
if exist %cd%\*.original (del %cd%\*.original)
if exist %cd%\*.ini (del %cd%\*.ini)

rem for /f %%i in ('dir %cd% /b/a:d') do echo %%i | find "_ert_rtw" && rd /s /Q %cd%\%%i

for /d %%d in (*) do (
    rem 获取文件夹名称（不带路径）
    set "folder=%%~nd"
    rem 截取文件夹名称的最后8个字符（因为"_ert_rtw"刚好8个字符）
    set "suffix=!folder:~-8!"
    
    rem 判断截取的后缀是否为"_ert_rtw"
    if "!suffix!"=="_ert_rtw" (
        rem 强制删除文件夹及内容
        rd /s /q "%%d"
        echo 已删除：%%d
    )
)