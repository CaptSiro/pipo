$clion = "$env:LOCALAPPDATA\Programs\CLion\bin"
$env:PATH = "$clion\mingw\bin;$clion\cmake\win\x64\bin;$clion\ninja\win\x64;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
