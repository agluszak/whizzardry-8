# Rendering-library prototypes

Evaluation code, not part of the game. Each prototype renders the same scene
through one rendering library so that libraries can be compared on the work
that would actually replace the native renderer.

The scene is built with the game's own render types (`Mesh`, `Material`,
`Light`, `FogParams`, `Texture`, `Camera3D`), including an animated mesh whose
frames go through the compressed frame codec used by `mesh_loader.cpp`.

## What is shared (`common/`)

- `resident_mesh`: the GPU-resident layout of a `Mesh`, built once. Every frame
  stays on the GPU; UV-seam corners, per-vertex materials and baked DIG colours
  become vertex attributes. The native renderer rebuilds this every frame.
- `shaders/wizardry_vertex.glsl`: a GLSL port of `shadeVertices` for one vertex,
  plus frame interpolation (positions interpolated, normals from the current
  frame, as `Mesh::GetVertexLocations` does). `wizardry_fragment.glsl` is the
  existing `DrawShader` combiner.
- `shading_block`: the per-object CPU work that remains in any design (light and
  fog selection, material packing), packed as a flat `vec4` array that every
  library can bind.
- `transparent_order`: the native global triangle sort (same keys, same
  stability, clip-space depth as the native path) turned into draw runs over a
  per-frame index buffer, so geometry stays resident.
- `lighting_check`: runs the native `shadeVertices` on the same inputs and
  compares it with the GPU vertex stage, one pixel per vertex.

A prototype only has to upload the resident data, map `DrawShader` state to the
library's pipeline state, and submit the opaque draws and the ordered
transparent runs.

## Running

Configure with `-DWIZ8_RENDER_POC=ON` plus the prototype's own option, then:

```sh
build/linux/wiz8_render_poc_<library> --verify --frames 300 --screenshot frame.png
```

`--verify` exits with status 1 when any vertex differs from `shadeVertices` by
more than 1.5/255. `--time SECONDS` freezes the animation, `--size WxH` sets the
window size, and without `--frames` the window stays open until closed.
