# Contributing

Contributions that improve native Windows compatibility, registered-v1.1
behavioral parity, tests, documentation, or code quality are welcome.

## Before submitting a change

1. Use a legally acquired registered-v1.1 copy of *Hocus Pocus*.
2. Keep original game files and extracted assets out of commits.
3. Build and run the complete test suite:

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

4. Document disassembly-backed behavior with exact segment:offset evidence.
5. Keep native extensions disabled by default and separate from the 1:1 path.

## Copyrighted material

Never attach or commit original executables, archives, saves, converted assets,
screenshots containing substantial original artwork, music, or sound files.
Pull requests containing commercial game data will be closed.
