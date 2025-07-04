# tui-music-player


## Build
```
git clone --recursive https://github.com/VimHater/tui-music-player.git
cd tui-music-player
mkdir build/
cd build/
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Generate sln
```
mkdir .\build\
cd .\build\
cmake -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" ..
cmake --build .
```
