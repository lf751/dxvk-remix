# Neural rendering DLL compatibility

DLSS-NR normally uses the NGX feature loader. It checks the DLL's signature, metadata and GPU support before creating a feature. A file being present in the runtime folder does not establish availability.

`rtx.neuralUplift.useDirectLibrary` selects the direct DLL backend. It defaults off and is independent of graphics presets. The Remix menu exposes it as **Direct DLL Compatibility**, beside **Enable DLSS-NR**. Use it only with a compatible DLL and permission covering that integration.

The direct backend uses the runtime directory's `nvngx_dlssnr.dll`, followed by the normal game search directories. Restart after adding or replacing that file. Changing the backend, network preset or pass count recreates the features and resets their temporal histories.

The DLL session owns caller-origin compatibility handling and survives while any pass owns a feature. Each pass owns its own handle and parameter block. Handles are released after the GPU drains, and the DLL is shut down only after the last handle is released. NGX calls and direct calls use the same serialization lock.

The direct path supplies signed subrect dimensions consumed by the snippet interface. It preserves the normal material control mask, automatic mask blend mode, depth, motion vectors and neural-rendering intensity controls.

The log identifies the selected backend when creating a feature and records its first successful evaluation. A successful initialization or evaluation return code establishes execution, not a visible image improvement; compare the enabled and disabled result in the target scene.

Do not redistribute a locally patched DLL as part of the runtime package. The build output retains its original SDK DLL.
