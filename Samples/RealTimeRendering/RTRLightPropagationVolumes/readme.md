# Light Propagation Volumes (LPV)

A MiniEngine-based demo of **Light Propagation Volumes**, the lattice-based dynamic
global-illumination technique from Kaplanyan & Dachsbacher, *"Cascaded Light
Propagation Volumes for Real-Time Indirect Illumination"* (I3D 2010).

The sample renders the Sponza atrium with one bounce of fully dynamic indirect
lighting: move the sun and the colored bounce light (red/green from the curtains onto
the floor and columns) updates in real time with no precomputation.

## How it works

The direct-lit scene (sun + shadows + SSAO) is produced by MiniEngine's Sponza
renderer. The LPV indirect light is then layered on with these extra GPU passes each
frame:

1. **Albedo G-buffer** – a depth-equal pass writes per-pixel albedo (needed for the
   `indirect × albedo` term).
2. **Reflective Shadow Map (RSM)** – the scene is rendered from the sun into flux /
   normal / depth targets; each texel is a Virtual Point Light (VPL).
3. **Injection** – every RSM texel is splatted as a 2-band spherical-harmonics cosine
   lobe into three RGB 3D textures (one per color channel). A geometry shader routes
   each VPL to the correct grid slice via `SV_RenderTargetArrayIndex`.
4. **Geometry Volume injection** – RSM surfels are injected as fuzzy occluders to block
   light propagation through walls.
5. **Propagation** – a compute shader iteratively gathers SH flux from the six
   face-adjacent neighbours (with geometry-volume occlusion), accumulating the result.
6. **Composite** – a fullscreen pass reconstructs world position from depth, samples
   the LPV, and adds `indirect × albedo` to the HDR scene color.

The grid is a single 32³ cascade fit to the scene bounds; the RSM is 1024².

## Controls / Tuning

Open the engine tuning menu (`Back`/`~`) → **LPV**:

- **Debug View** – Final / Direct only / Indirect only / Albedo / Normal
- **Propagation Steps** – number of propagation iterations (light spread distance)
- **Inject / Indirect Scale** – VPL flux and indirect intensity
- **Use Geometry Volume**, **Occlusion Amp** – fuzzy occlusion controls

Sun direction and intensity are under the existing **Sponza/Lighting** menu and drive
both the direct light and the RSM.

## Requirements

Windows 10 SDK 19041+, Visual Studio 2019/2022, a Direct3D 12 GPU. Runs with
`MiniEngine\ModelViewer` as the working directory (preset for the debugger).
