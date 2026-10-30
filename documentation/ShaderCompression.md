# Embedded shader compression

The build option `compress_embedded_shaders` losslessly packs the RTX SPIR-V embedded in the runtime with the existing
DXVK word codec. It defaults off. Runtime-generated `.spv` files and the non-RTX GLSL headers remain raw SPIR-V.
The shader manager accepts both formats and caches the decoded owned code for each shader name.

Build a compressed release using the normal supported wrapper:

```powershell
.\scripts\build.ps1 -Flavor release -Jobs 4 -MesonArgs '-Dcompress_embedded_shaders=true'
```

Use `-Dcompress_embedded_shaders=false` to return to raw embedded shaders. The header-mode stamp forces regeneration
when changing modes. It is written only after all shader compilation succeeds. The release SPIR-V optimization and
validation passes still run before header encoding. Shader variants and compile-time feature defines are unchanged.
The runtime recompilation arguments are copied before the build-only compression flag is appended, so hot reload
continues to produce raw SPIR-V rather than compressed headers.

The format is little-endian: a 16-byte header (magic `0x43565053`, original word count, payload byte count, zero),
then one 64-bit byte-width mask for each 32 original words, then the packed payload padded to eight bytes.
The decoder validates header size, reserved value, payload size, mask byte counts, and decoded SPIR-V magic.

Compression reduces stored shader bytes rather than GPU shader memory. First use adds CPU decoding and temporary
buffers. Driver pipeline compilation is unchanged. Measure the final package size and game load time on the shipping
build; representative shader ratios do not predict the whole DLL size.

The dedicated `test_embedded_spirv` unit target verifies exact Python/C++ round trips, raw passthrough, all word widths,
partial mask groups, malformed lengths/counts/masks, reserved fields, and decoded magic. Build it in `_Comp64UnitTest`
and run it with `meson test -C _Comp64UnitTest --no-rebuild --verbose test_embedded_spirv` after compilation.
