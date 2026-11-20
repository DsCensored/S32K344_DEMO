function A2LSetAddr()

clc;
clear;

delete('.\*.a2l')

FileFolder=[cd,'\A2LFile'];
if ~exist(FileFolder,'dir')
    error(['未找到',FileFolder,'文件夹'])
end

%[filename, ~] = uigetfile({'*.a2l'},'选择需要填写地址的A2L文件',FileFolder,'MultiSelect','on');

filename = {'Cali.a2l'};


srcpath = '..\S32K344_S32DS\S32K344\Debug_FLASH\S32K344.elf';
copyfile(srcpath,'.\');
FileName_elf = 'S32K344.elf';
MKA2LSetAddr(filename,FileName_elf);

delete('.\*.elf')

end





function MKA2LSetAddr(filename,FileName_elf)    


    FileFolder=[cd,'\A2LFile'];
    if ~exist(FileFolder,'dir')
        error(['未找到',FileFolder,'文件夹'])
    end

    %% A2L文件配置
    %需要将编译后的.elf文件放入模型文件夹，配置.elf文件名称等配置项
    %FileName_elf = uigetfile({'*.elf;*.axf','VarAddrFile(*.elf,*.axf)'},'选择需要生成A2L文件的.elf/.axf文件','MultiSelect','on');%elf文件名称
    if isequal(FileName_elf,0)
        error('未选择.elf/.axf文件');
    end
    if ~iscell(FileName_elf)
        FileName_elf = cellstr(FileName_elf);
    end

   % [filename, ~] = uigetfile({'*.a2l'},'选择需要填写地址的A2L文件',FileFolder,'MultiSelect','on');
    if isequal(filename,0)
        error('未选择A2L文件');
    end

    ElfNum = length(FileName_elf);%获取当前文件夹下的模型数量
    A2LFileNameCell = cellstr(filename);
    FileNum = length(A2LFileNameCell);

    for n=1:1:ElfNum
        for i=1:1:FileNum
            copyfile([FileFolder,'\',A2LFileNameCell{i}],[cd,'\',A2LFileNameCell{i}]);
        end
    
        for i=1:1:FileNum
            try
                rtw.asap2SetAddress(A2LFileNameCell{i},FileName_elf{n});
            catch ME
                disp(ME.message);
            end
        end
    
        mergeA2LFile(FileName_elf{n},A2LFileNameCell,FileNum);
    
        for i=1:1:FileNum
            delete([cd,'\',A2LFileNameCell{i}]);
        end
    end
end

%% 合成新的A2L文件
function mergeA2LFile(FileName,MdlNameCell,MdlNum)
    index = strfind(FileName,'.');
    FileName = strrep(FileName,FileName(index:end),'.a2l');
    
    f_write = fopen(FileName,'wt+'); 
    for num = 1:1:MdlNum
        A2lFileName = MdlNameCell{num};
        if exist(A2lFileName,'file')
            str = fileread(A2lFileName);
    
            %查找未填充地址的变量
            expression = '/\* @ECU_Address@.*?@ \*/';
            [startIndex,endIndex] = regexp(str,expression);
            
            signalNum = length(startIndex);
            while(signalNum > 0)
                signalOrgName = str(startIndex(1)+16:endIndex(1)-4);%查询变量名称

                %处理信号名称中的正则表达式元字符
                expression = '((\[)|(\])|(\.)|(\*)(\^)|(\$)|(\+)|(\?)|(\{)|(\})|(\\)|(\|)|(\()|(\)))';
                replace = ['\\','$1'];
                signalName = regexprep(signalOrgName,expression,replace);
    
                %删除变量相关描述
                expression = [' *?/begin ((CHARACTERISTIC)|(MEASUREMENT)|(AXIS_PTS))\s*?/\* Name                   \*/\s*?',signalName,'[\s\S]*?/end ((CHARACTERISTIC)|(MEASUREMENT)|(AXIS_PTS))\s*?\n\s*?\n'];
                replace = '';
                str = regexprep(str,expression,replace);

                expression = [' *?/begin ((CHARACTERISTIC)|(MEASUREMENT)|(AXIS_PTS))\s*?/\* Name                   \*/\s*?',signalName,'[\s\S]*?/end ((CHARACTERISTIC)|(MEASUREMENT)|(AXIS_PTS))'];
                replace = '';
                str = regexprep(str,expression,replace);
            
                expression = ['\n\s*?',signalName];
                replace = '';
                str = regexprep(str,expression,replace);
            
                %重新查询变量（因为上面删除部分变量后，变量位置发生变化）
                expression = '/\* @ECU_Address@.*?@ \*/';
                [startIndex,endIndex] = regexp(str,expression);
                
                searchIndex = signalNum;
                signalNum = length(startIndex);

                if (searchIndex == signalNum)
                    clc;
                    error([A2lFileName,'文件中的',signalOrgName,'信号命名异常']);
                end                
            end

            expression = '((REF_CHARACTERISTIC)|(REF_MEASUREMENT)|(REF_AXIS_PTS))[\s\S]*?(\n)';
            replace = '$1$2';
            str = regexprep(str,expression,replace);

            expression = '\n\s*/begin MODULE[\s\S]*?/end MODULE';
            [startIndex,endIndex] = regexp(str,expression);
    
            if(1==num)
                startIndex = 1;
            end
    
            fprintf(f_write,'%s\n',str(startIndex:endIndex));  
        end
    end

    fprintf(f_write,'%s\n','/end PROJECT');    
    fclose(f_write);
end