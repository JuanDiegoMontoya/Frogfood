setlocal
set d=%DATE:~-4%-%DATE:~4,2%-%DATE:~7,2%
set t=%time::=.% 
set t=%t: =%

break>comdat_dump.txt
for /R "..\out\build\x64-Debug\CMakeFiles\frogRender.dir\" %%n in (*.obj) do "DumpBin.exe" /headers "%%n" >> comdat_dump.txt
.\SymbolSort.exe -in:comdat .\comdat_dump.txt > "comdat_anal_%d%_%t%.txt"