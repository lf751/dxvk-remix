# Bridge private-data CPU regression

Run `run-private-data-tests.ps1 -OutputDirectory <absolute disposable folder>`
from PowerShell. It builds x86 with Visual Studio, debug CRT, warnings as errors,
and includes the real `d3d9_privatedata.h`. It does not load the runtime or use the
GPU. Generated EXE/PDB/OBJ files stay in the supplied output directory.

The suite checks independently owned byte copies, repeated retrieval,
replacement/free/destruction, distinct GUIDs, small-buffer canaries, size queries,
invalid arguments, COM AddRef/Release accounting, same-object replacement,
byte/interface replacement under one GUID and reentrant release after removal.

The original factory shallow-copied owning byte buffers both when inserting a
temporary and when retrieving a map value. COM replacement also overwrote the old
pointer without releasing it and released the temporary's new reference. A
baseline CPU reproduction confirmed old COM count 2 instead of 1 and replacement
count 1 instead of 2. The original header's unused build-only includes were
removed solely to compile that isolated reproduction; its class was unchanged.

The corrected store uses immutable owned entries shared across a lookup/removal,
a byte vector or an owned COM reference per entry, and one GUID namespace. COM
callbacks run outside the map lock. Resource wrappers permit null-buffer size
queries, while the factory enforces capacity and returns D3DERR_MOREDATA,
D3DERR_NOTFOUND or D3DERR_INVALIDCALL as appropriate.

Microsoft API reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-getprivatedata

Validation: 42 CPU checks passed, both bridge architectures built successfully.
No GPU/game validation is claimed by these tests.
