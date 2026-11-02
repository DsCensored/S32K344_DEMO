# replace_can_id.ps1
$ErrorActionPreference = 'Stop'

# 设置你要替换的数值和宏
$SearchVal = '536870910'
$TargetMacro = 'CAN_ID_VEH_TX_NM'

Write-Host "--- 开始执行 PowerShell 替换 ---"

# 获取当前脚本所在目录
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir = Join-Path $ScriptDir "AutoSarNm_ert_rtw"

if (-not (Test-Path $BuildDir)) {
    Write-Host "未找到目录: $BuildDir" -ForegroundColor Red
    exit
}

# 查找所有的 .c 和 .h 文件
$files = Get-ChildItem -Path $BuildDir -Include *.c,*.h -Recurse
$count = 0

foreach ($f in $files) {
    # 强制去除只读属性，防止因只读导致写入失败
    if ($f.IsReadOnly) {
        Set-ItemProperty -Path $f.FullName -Name IsReadOnly -Value $false
    }

    # 使用系统默认编码读取（兼容中英文代码）
    $content = [System.IO.File]::ReadAllText($f.FullName, [System.Text.Encoding]::Default)
    
    # 匹配目标值，包含可选的 U 后缀，并确保不是较长数字的一部分
    if ($content -match "(?<![0-9a-zA-Z_])$SearchVal(?:[uU]?[lL]{0,2})\b") {
        Write-Host "发现目标: $($f.Name)" -ForegroundColor Yellow
        
        # 执行替换
        $newContent = $content -replace "(?<![0-9a-zA-Z_])$SearchVal(?:[uU]?[lL]{0,2})\b", $TargetMacro
        
        # 写入文件
        [System.IO.File]::WriteAllText($f.FullName, $newContent, [System.Text.Encoding]::Default)
        
        # 校验是否真的成功
        $checkContent = [System.IO.File]::ReadAllText($f.FullName, [System.Text.Encoding]::Default)
        if ($checkContent -match $TargetMacro) {
            Write-Host "? 替换成功并校验通过: $($f.Name)" -ForegroundColor Green
            $count++
        } else {
            Write-Host "? 写入失败，请检查是否被占用: $($f.Name)" -ForegroundColor Red
        }
    }
}

if ($count -gt 0) {
    Write-Host "--- 执行完成，共修改 $count 个文件 ---" -ForegroundColor Green
} else {
    Write-Host "--- 未找到需要替换的数值 ---" -ForegroundColor Cyan
}