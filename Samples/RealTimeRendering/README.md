# Real-Time Rendering Samples

This directory collects self-contained demos of real-time rendering techniques,
built on top of the **MiniEngine** framework (`MiniEngine/Core` + `MiniEngine/Model`)
in the same spirit as `Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample`.

Each technique lives in its own folder with a Visual Studio solution under `src/`:

| Sample | Technique |
|--------|-----------|
| [RTRLightPropagationVolumes](RTRLightPropagationVolumes/) | Light Propagation Volumes (LPV) dynamic global illumination |

## Building

Open `<Sample>/src/<Sample>.sln` in Visual Studio 2019/2022 (toolset v143, Windows 10
SDK 19041+) and build `x64`. The solutions reference the shared `MiniEngine\Core` and
`MiniEngine\Model` projects and restore their NuGet packages on first build.

These samples reuse the Sponza model that ships with `MiniEngine\ModelViewer\Sponza`,
so they run with that folder as the working directory (already configured as the
project's debugger working directory).
