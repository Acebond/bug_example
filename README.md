# LiteFX DirectX 12 structured-buffer reproducer

Standalone console test of a packed `StructuredBuffer<InstanceData>` through
LiteFX's reflected descriptor-set path. It has no scene, mesh, textures, asset
loader, or render loop. The input is generated on the CPU; a single-thread compute
shader reads it and writes diagnostics to a raw buffer for CPU readback.

A hidden Win32 window exists only because the inspected LiteFX device API
requires a surface/swap chain. Nothing is rendered or presented.

## Build

Requirements: Windows, an x64 Visual Studio developer shell with C++23 support,
CMake 4.2+, Ninja, vcpkg (`VCPKG_ROOT` set), and Windows Graphics Tools for the
D3D12 debug layer. The application explicitly enables validation in every build.

From this folder in an x64 developer PowerShell:

```powershell
cmake --preset x64-Debug
cmake --build --preset x64-Debug --target bug_example
./out/build/x64-Debug/bin/bug_example.exe
```

CMake fetches LiteFX revision `7ed63e973434d90f8dc00be6570e20a7348ec00c`.
Only the DX12 backend is built. The parent game project is not used or changed.

To test a local LiteFX checkout (including a proposed fix):

```powershell
cmake --preset x64-Debug -DFETCHCONTENT_SOURCE_DIR_LITEFX=C:/path/to/LiteFX
cmake --build --preset x64-Debug --target bug_example
```

The override points to the LiteFX repository root, containing `src/CMakeLists.txt`.
When using the override, record that checkout's actual commit and changes; the
startup banner identifies only the default pinned revision.

## Run and capture

```powershell
# Small packed array: 8 records, 640-byte blocks, three different blocks.
./out/build/x64-Debug/bin/bug_example.exe 2>&1 | Tee-Object validation-small.log

# Original application-sized array: 8192 records, 655360-byte blocks.
./out/build/x64-Debug/bin/bug_example.exe --large 2>&1 | Tee-Object validation-large.log

# Control run: ordinary debug layer remains enabled; GPU validation is disabled.
./out/build/x64-Debug/bin/bug_example.exe --no-gpu-validation
```

GPU validation is enabled before device creation. Debug breaks are disabled
after device creation so validation errors can be collected without a debugger.
The program waits after every dispatch/copy, prints GPU readback and stored D3D12
messages, then exits. Codes: `0` = dimensions/data match with no error/corruption
messages; `1` = setup/execution failure; `2` = validation error or readback mismatch.
Correct readback alone does not establish that the descriptor is valid.

## Expected results

`InstanceData` contains a row-major 4x4 float matrix and float4 color: 80 bytes.
One LiteFX buffer element contains an entire array; there are three elements.
Each descriptor selects exactly one of those blocks, as in a per-frame buffer.

The shader reports `GetDimensions` and reads records 0, 1 and 7. The expected
stride is 80 and count is 8 (or 8192 with `--large`) for every block. Matrix
sentinels are `10000,10001,10007` in block 0, `20000,20001,20007` in block 1,
and `30000,30001,30007` in block 2. Color sentinels add 0.25 to the record value.

At the pinned source revision, inspect
`src/Backends/DirectX12/src/descriptors.hpp`, in the `StructuredBuffer` case:

```cpp
.FirstElement = element,
.NumElements = 1,
.StructureByteStride = static_cast<UInt32>(elementSize)
```

For the small test, the intended `(FirstElement, NumElements, stride)` values
are `(0,8,80)`, `(8,8,80)`, `(16,8,80)`. The source constructs `(0,1,640)`,
`(1,1,640)`, `(2,1,640)`. The large test should use first elements
`0,8192,16384`, count `8192`, stride `80`.

The small test deliberately stays below the 2048-byte D3D12 structure-stride
limit, isolating a shader/view mismatch from the oversized stride of the large
case. Runtime diagnostics and apparent rendering/readback behavior may differ
between drivers; attach the actual logs rather than assuming a particular error.

The UAV output is a `RWByteAddressBuffer` to keep the structured-SRV problem
isolated. This reproducer does not separately test `RWStructuredBuffer`.

## Verified results

Built and executed on 25 October 2026 with the pinned LiteFX revision,
MSVC 19.51, and an NVIDIA GeForce RTX 4080:

| Run | GPU validation | Readback, all three blocks | Validation errors | Exit code |
| --- | --- | --- | --- | --- |
| Default, 8 records/block | On | Matches | 15 stride mismatches | 2 |
| `--large`, 8192 records/block | On | Matches | 15 stride mismatches | 2 |
| `--no-gpu-validation`, 8 records/block | Off | Matches | 0 | 0 |

The stored D3D12 diagnostic is message **1387**:

```text
GPU-BASED VALIDATION: Dispatch, Structured buffer byte stride mismatch:
Resource: ...:'Packed instances', Descriptor Type: SRV,
StructureByteStride in UAV/SRV desc: 640,
ByteStride for struct used in shader: 80.
```

The large run reports the same diagnostic with descriptor stride `655360`.
On this driver, `GetDimensions` returns the expected count and stride and all
tested values match even while GPU validation reports the descriptor mismatch.
The absence of visible/data errors or a successful `GetDimensions` check is
therefore insufficient to validate the SRV declaration.

The original test logs are in `out/validation-small.log`,
`out/validation-large.log`, and `out/validation-control.log` in this working copy.
They are generated files and excluded from Git; the commands above recreate them.

## References

- [D3D12 structured SRV requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv)
- [GPU-based validation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)
- [Inspected LiteFX descriptor code](https://github.com/crud89/LiteFX/blob/7ed63e973434d90f8dc00be6570e20a7348ec00c/src/Backends/DirectX12/src/descriptors.hpp)
