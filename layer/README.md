# layer

The custom layer, based on [B-Y-P/4coder_byp](https://github.com/B-Y-P/4coder_byp)
with parts of the Fleury (rjf) layer ported in.

- `4coder_byp.cpp` — entry point; includes everything below
- `4coder_byp_*.cpp/.h` — layer code (bindings, hooks, drawing, tabs/sessions, ...)
- `4coder_vimrc.h` — vim configuration
- `vim/` — vim emulation module
- `custom/` — patched copy of the 4coder custom framework the layer compiles against

## Building

From the repo root:

```
./build.sh [release]      # Linux / macOS
build.bat [release]       # Windows (needs cl.exe in PATH)
```

Output goes to `build/custom_4coder.so` (`.dll` on Windows), next to `4ed`.
