# LLVM Installation Guide

## Current Status

LLVM is **temporarily disabled** in the Zero compiler build. The display module (print, log, error formatting) is complete and working without LLVM.

## Why LLVM is Disabled

Two incomplete LLVM installations were found:

1. **C:\mingw64** - Has `llvm-config` but missing library files
2. **C:\Program Files\LLVM** - Has some libraries but only C API, not full C++ development kit

Neither installation has the complete set of C++ libraries needed for compiler development.

## When You Need LLVM

LLVM is required for:

- Lexer implementation
- Parser implementation
- AST construction
- Code generation (LLVM IR)
- JIT compilation

**Current working components** (no LLVM needed):

- ✅ Runtime library (`print`, `log`)
- ✅ Diagnostics library (error formatting)
- ✅ All tests passing

## How to Install LLVM (When Ready)

### Recommended: Official Pre-built Binaries

1. **Download LLVM 18.1.8**:
   https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/LLVM-18.1.8-win64.exe

2. **Run installer**:

   - Install to: `C:\LLVM`
   - ✅ Check "Add LLVM to system PATH"

3. **Verify installation**:

   ```powershell
   llvm-config --version
   llvm-config --libdir
   ```

4. **Enable in CMakeLists.txt**:

   - Open `CMakeLists.txt`
   - Find the LLVM Configuration section (line ~9)
   - Uncomment the LLVM lines
   - Update `LLVM_DIR` if needed:
     ```cmake
     set(LLVM_DIR "C:/LLVM/lib/cmake/llvm")
     ```

5. **Rebuild**:
   ```bash
   cd build
   cmake ..
   cmake --build . --config Release
   ```

### Alternative: Chocolatey

```powershell
choco install llvm -y
```

Then follow steps 3-5 above.

## Summary

**For now**: Continue development without LLVM  
**Later**: Install LLVM when ready to build compiler components  
**Instructions**: Clearly marked in `CMakeLists.txt`
