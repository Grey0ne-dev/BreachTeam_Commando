cmake -S . -B build-local -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build-local --parallel
  ./build-local/breach_team --sdl
