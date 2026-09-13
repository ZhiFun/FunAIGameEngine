@echo off
cd /d F:\New_Project\FunAIGameEngine
taskkill /IM fun_sim.exe /F >nul 2>&1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "build\Desktop_Qt_6_9_3_MinGW_64_bit-Debug" > build_res.txt 2>&1
echo EXIT=%ERRORLEVEL% >> build_res.txt
