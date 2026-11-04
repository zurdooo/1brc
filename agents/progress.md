# Progress

## Done

- 2026-11-04: Ported `cpp/src/1brc.cpp` to build natively on Windows with MSYS2 UCRT64 g++ 15.2. A `_WIN32` branch uses `CreateFileMapping`/`MapViewOfFile` in place of `mmap`; the POSIX path is unchanged. `uint` was replaced with `unsigned int`, since MinGW lacks it.
  - Build from `cpp/` in a UCRT64 shell: `g++ -std=c++23 -O3 -march=native src/1brc.cpp -o 1brc.exe -lstdc++exp` (MinGW needs `-lstdc++exp` for `std::print`).
  - Run from `cpp/`; the program reads `../measurements.txt`.
  - Verified on 10M rows against an independent Python aggregation: station names, min and max all match.

## Open

- Mean rounding bug in `output_stations`: it adds `count / 2` to the tenths total and then divides as a double, so every mean is about +0.05 high before `{:.1f}` rounds. Only 181 of 413 means matched the true values in the 10M-row check.
- `one_br_perf_2` drops the `size % NUMBER_OF_THREADS` remainder bytes; it counted 9,999,998 newlines for 10M rows.
- `compare_cpp_output.sh` compiles `cpp/1brc.cpp`, but the source is at `cpp/src/1brc.cpp`.
- `cpp/1brc.exe` is not in `.gitignore`.
- On this Windows machine the Java generator can't be built (only Java 8 is installed).
