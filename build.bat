@ECHO OFF
set command=g++ main.cpp -o build/main.exe -I D:\libs\json\include -I D:\libs\boost_1_80_0 -l ws2_32
if "%1" == "release" (
    set command=%command% -O3
) else (
    set command=%command% -g
)
%command%