# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

All samples use Visual Studio MSBuild (`.sln` / `.vcxproj`). There is no CMake or cross-platform build.

**Open a sample:**
Each sample has its own solution file at `Samples/Desktop/<SampleName>/src/<SampleName>.sln`. Open it in Visual Studio 2019+ and build with the standard UI or:

```powershell
msbuild Samples\Desktop\D3D12HelloWorld\src\D3D12HelloWorld.sln /p:Configuration=Release /p:Platform=x64
```

**NuGet restore** is required before first build — Visual Studio handles this automatically. Packages land in `Samples/Desktop/<SampleName>/src/packages/`.

**Output binaries** land at `bin\x64\<Configuration>\` relative to each sample's `src\` directory.

**Requirements:** Windows 10 SDK 19041 (2004), Visual Studio 2019+ (toolset v143). Some samples (Mesh Shaders, Raytracing, VRS) require a DirectX 12 Ultimate GPU.

## Repository Layout

- `Samples/Desktop/` — ~24 standalone D3D12 samples; each is self-contained with its own `.sln`
- `Samples/RealTimeRendering/` — MiniEngine-based real-time rendering technique demos (e.g. `RTRLightPropagationVolumes`); each subfolder has its own `src/<Name>.sln` referencing `MiniEngine/Core` + `MiniEngine/Model`
- `Libraries/` — Shared code: D3DX12 helpers, D3DX12Residency, D3DX12AffinityLayer (multi-GPU), D3D12RaytracingFallback
- `MiniEngine/` — Full engine starter kit (Core lib + ModelViewer app + asset converter)
- `Packages/` — Vendored NuGet packages (DirectXMesh, DirectXTex, D3D12 headers, DXC)
- `TechniqueDemos/` — Deeper technique demos (e.g. D3D12MemoryManagement)
- `Tools/` — Standalone utilities (DXGIAdapterRemovalSupportTest)

## Sample Architecture

Every `Samples/Desktop/` sample follows the same skeleton:

| File | Role |
|---|---|
| `DXSample.h/.cpp` | Abstract base; virtual `OnInit/OnUpdate/OnRender/OnDestroy` |
| `DXSampleHelper.h` | `ThrowIfFailed()`, `HrException`, asset loading, shader compile helpers |
| `Win32Application.h/.cpp` | Window creation and Win32 message loop |
| `Main.cpp` | Entry point, instantiates the concrete sample and `Win32Application::Run()` |
| `<SampleName>.h/.cpp` | Concrete sample: all D3D12 resource setup and per-frame logic |

To add a new sample, replicate this four-file structure and subclass `DXSample`.

### Key Helpers (DXSampleHelper.h)

- `ThrowIfFailed(hr)` — wraps every D3D12/DXGI call; throws `HrException` on failure
- `NAME_D3D12_OBJECT(obj)` — sets a debug name (critical for PIX and D3D12 debug layer output)
- `CalculateConstantBufferByteSize(n)` — rounds up to 256-byte CB alignment
- `ReadDataFromFile()` / `ReadDataFromDDSFile()` — asset loading utilities

All D3D12 resources are managed via `Microsoft::WRL::ComPtr<T>`.

## MiniEngine

`MiniEngine/` is an independent engine with its own property sheets (`PropertySheets/Build.props`, `Desktop.props`). Entry point is `ModelViewer/`. It does not share the `DXSample` base class used by `Samples/Desktop/`. Use `MiniEngine/CreateNewSolution.bat` to scaffold a new app against the engine.

## Coding Conventions

- `Microsoft::WRL::ComPtr` for all COM/D3D12 object lifetime management
- `ThrowIfFailed()` wraps every HRESULT-returning call
- `WIN32_LEAN_AND_MEAN` + `NOMINMAX` in every PCH
- Wide strings (`wchar_t`, `LPCWSTR`) for all Windows API paths
- Standard PCH: `stdafx.h` (older samples) or explicit includes in newer ones
