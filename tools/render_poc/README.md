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
- `shading_block`: the shader inputs grouped by owner and rate of change: a
  view block (environment, every prepared light and fog layer), an instance
  block (frames, bounds, exclusion mask, fade and glow overrides), a pass block
  (materials) and a batch block (`DrawShader` combiner state). The per-object
  light and fog rejections of `shadeVertices` run in the shader against the
  instance's eye-space bounds, so one view block serves every object.
- `transparent_order`: the native global triangle sort (same keys, same
  stability, clip-space depth as the native path) turned into draw runs over a
  per-frame index buffer, so geometry stays resident.
- `lighting_check`: runs the native `shadeVertices` on the same inputs and
  compares it with the GPU vertex stage, one pixel per vertex.

## VulkanSceneGraph: the scene graph, not only the renderer

`vsg/world.h` replaces `WorldScene`'s flat attach lists with a VSG graph built
from the game's objects. Nodes borrow `MeshInstance`, `Light` and `FogParams`
and read them during traversal, so game code keeps writing poses, frames and
fades on its own objects with no synchronisation step. VSG then owns:

- hierarchy: an `InstanceTransform` per instance and a `VertexAttachment` that
  follows a mapped vertex at the current animation frame. The lantern rides the
  lead creature's "hand", and its lamp lights the scene from there;
- culling: a `CullNode` per instance, tested by VSG's record traversal;
- light collection: `LightNode`s report their accumulated model-view to the
  view's `WorldViewState` (a `vsg::ViewDependentState`), which prepares them as
  `Light::beginPass` does and packs the per-view block bound with
  `BindViewDescriptorSets`;
- translucency: sorted and fading instances defer to a final
  `TranslucentStage`, which runs the native global triangle order over
  everything the view collected;
- picking: `vsg::LineSegmentIntersector`, through an `ArrayState` that exposes
  the instance's current frame and honours `RENDER_NO_PICK`;
- lifetime: attached nodes compile through the viewer's `CompileManager` before
  they join the graph, and detached ones are released once no frame in flight
  uses them. A creature of a new mesh kind arrives and leaves while the demo runs.

Shared GPU state (one `MeshAsset` per `Mesh`, pipelines per `DrawShader` word)
is created once; an instance adds only its own small uniform block.

Not covered yet: the highlight shell pass, particles (whose simulation runs
inside the native draw today), ground-shadow decals, mesh parts and damage
skins, the sky scene and the automap's camera. Octree visibility would write
node masks instead of `MeshInstance::visible`.

## Running

Configure with `-DWIZ8_RENDER_POC=ON` plus the prototype's own option, then:

```sh
build/linux/wiz8_render_poc_<library> --verify --frames 300 --screenshot frame.png
```

`--verify` exits with status 1 when any vertex differs from `shadeVertices` by
more than 1.5/255, or when picking returns the wrong instance. `--herd N` sets
the number of creatures for scale tests, `--time SECONDS` freezes the
animation, `--size WxH` sets the window size, and without `--frames` the window
stays open until closed.
