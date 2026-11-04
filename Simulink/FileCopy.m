function FileCopy(filename)
%FILECOPY 把 Simulink 模型生成的 C/H 文件复制到 S32DS 工程中。
%
%   FileCopy('ASP')    % 指定模型名
%   FileCopy           % 不指定时，取当前打开模型的名字
%
% 复制规则：
%   1) 属于模型 XXX 的 C/H 文件（XXX.c、XXX.h、XXX_private.h、XXX_types.h ...）
%      复制到 <工程根>\S32K344_S32DS\S32K344\ASW\Model\XXX\
%      目标目录不存在会自动创建；已存在同名文件直接覆盖，保证与生成代码同步。
%
%   2) 文件名中含 "Cfg" 的 C/H 文件（如 XXX_Cfg.h）属于用户配置，只做首次初始化：
%      检查 <工程根>\S32K344_S32DS\S32K344\ASW\ModelCfg\XXX\
%        - 没有该文件：创建目录并复制
%        - 已有该文件：不操作，保留用户手工调整过的配置参数
%
% 前置条件：
%   - 本脚本放在 <工程根>\Simulink 目录下；
%   - 生成代码位于 <工程根>\Simulink\XXX_ert_rtw；
%   - 工程根目录为包含 S32K344_S32DS 的那一层，脚本会从自身位置向上查找。
%
% 注意：
%   - 只处理以模型名开头的 .c/.h 文件，库文件（如 Mon3tr_Lib.c/h）与
%     rtwtypes.h、a2l 等不在此脚本处理范围内；
%   - Cfg 文件落在 ModelCfg\XXX 下，需保证 .cproject 的头文件路径中包含
%     ASW\ModelCfg\XXX，否则 XXX.h 中的 #include "XXX_Cfg.h" 会找不到。

    if nargin < 1 || isempty(filename)
        try
            filename = bdroot(gcs);
        catch
            filename = '';
        end
        if isempty(filename)
            error('FileCopy:NoModel', ...
                '未指定模型名，且当前没有打开任何 Simulink 模型。用法：FileCopy(''ASP'')');
        end
    end
    filename = char(filename);

    % ---------------- 路径解析 ----------------
    scriptDir = fileparts(mfilename('fullpath'));
    if isempty(scriptDir)
        scriptDir = pwd;
    end
    repoRoot = findRepoRoot(scriptDir);

    srcDir   = fullfile(scriptDir, [filename '_ert_rtw']);          % 生成代码目录
    modelDir = fullfile(repoRoot, 'S32K344_S32DS', 'S32K344', 'ASW', 'Model',   filename);
    cfgDir   = fullfile(repoRoot, 'S32K344_S32DS', 'S32K344', 'ASW', 'ModelCfg', filename);

    if exist(srcDir, 'dir') ~= 7
        error('FileCopy:NoSrcDir', ...
            '找不到生成代码目录：%s\n请先在 Simulink 中为模型 "%s" 生成代码。', srcDir, filename);
    end

    checkuselessinport(srcDir, filename);

    % ---------------- 复制 ----------------
    fprintf('源目录 ：%s\n', srcDir);
    fprintf('Model   ：%s\n', modelDir);
    fprintf('ModelCfg：%s\n', cfgDir);

    exts = {'*.c', '*.h'};
    nModel = 0;     % 覆盖到 Model\XXX 的文件数
    nCfg   = 0;     % 新建到 ModelCfg\XXX 的文件数
    nSkip  = 0;     % 已存在、跳过的 Cfg 文件数

    for k = 1:numel(exts)
        fileList = dir(fullfile(srcDir, exts{k}));
        for i = 1:numel(fileList)
            name = fileList(i).name;
            if ~isModelFile(name, filename)
                continue;   % 不是该模型的文件（如 Mon3tr_Lib.c）
            end

            srcFile = fullfile(srcDir, name);

            if contains(name, 'Cfg')
                % Cfg 文件：ModelCfg 下没有才创建并复制，有则保持用户配置
                dstFile = fullfile(cfgDir, name);
                if exist(dstFile, 'file') == 2
                    fprintf('  [跳过] %s（ModelCfg 下已存在，保留现有配置）\n', ...
                        fullfile('ModelCfg', filename, name));
                    nSkip = nSkip + 1;
                    continue;
                end
                ensureDir(cfgDir);
                copyfile(srcFile, cfgDir);
                fprintf('  [新建] %s\n', fullfile('ModelCfg', filename, name));
                nCfg = nCfg + 1;
            else
                % 普通 C/H 文件：复制到 Model\XXX，同名文件覆盖
                ensureDir(modelDir);
                copyfile(srcFile, modelDir);
                fprintf('  [覆盖] %s\n', fullfile('Model', filename, name));
                nModel = nModel + 1;
            end
        end
    end

    fprintf('FileCopy 完成：Model\\%s 更新 %d 个文件，ModelCfg\\%s 新建 %d 个文件，跳过 %d 个已有 Cfg 文件。\n', ...
        filename, nModel, filename, nCfg, nSkip);
end

% ===================== 子函数 =====================

function root = findRepoRoot(startDir)
%FINDREPOROOT 从脚本所在目录向上查找包含 S32K344_S32DS 的工程根目录。
    root = '';
    d = startDir;
    for k = 1:8
        if exist(fullfile(d, 'S32K344_S32DS'), 'dir') == 7
            root = d;
            return;
        end
        parent = fileparts(d);
        if isempty(parent) || strcmp(parent, d)
            break;
        end
        d = parent;
    end
    error('FileCopy:NoRepoRoot', ...
        '未能从 %s 向上找到包含 S32K344_S32DS 的工程根目录，请确认脚本放在 <工程根>\\Simulink 下。', ...
        startDir);
end

function tf = isModelFile(name, modelName)
%ISMODELFILE 判断文件是否属于该模型：XXX.c/XXX.h 或以 XXX_ 开头。
    [~, base] = fileparts(name);
    tf = strcmp(base, modelName) || startsWith(base, [modelName '_']);
end

function ensureDir(dirPath)
%ENSUREDIR 目录不存在时创建。
    if exist(dirPath, 'dir') ~= 7
        [ok, msg] = mkdir(dirPath);
        if ~ok
            error('FileCopy:mkdirFailed', '无法创建目录 %s：%s', dirPath, msg);
        end
    end
end

function checkuselessinport(srcDir, filename)
%CHECKUSELESSINPORT 检查模型是否把未定义信号对象的信号接到了输入端口。
    c_file = fullfile(srcDir, [filename '.c']);
    if exist(c_file, 'file') ~= 2
        return;
    end
    file_content = fileread(c_file);
    if contains(file_content, [filename '_U.'])
        warnmsg = sprintf( ...
            ['存在无效的信号输入（信号名必须解析为 Simulink 信号对象），请检查模型：%s'], filename);
        try
            warndlg(warnmsg, 'Warning');
        catch
            warning('%s', warnmsg);
        end
    end
end
