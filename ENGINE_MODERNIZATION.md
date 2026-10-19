# Engine, SurRender, and SGP Modernization Plan (Big-Bang Architecture)

This document specifies the **complete architectural replacement** of `src/sgp`, `src/surrender`, and `src/wiz8/engine_code` in a single, cohesive design.

Instead of incremental half-steps, adapters, or moving legacy classes into new folders, this plan **deletes the 2001 three-engine seam (`SGP` + `SurRender` + `engine_code`) outright** and replaces it with a clean, idiomatic C++23 architecture built directly on SDL3, SDL3_GPU, SDL3_mixer, and GLM.

---

## 1. Core Architectural Shift: From Three 1999 Engines to One Native Engine

The retail binary was complicated because Sir-Tech glued together three separate 1990s codebases with incompatible assumptions:
1. **SGP** (Jagged Alliance 2's 16-bit software blitter, STI sprites, and Win32 managers),
2. **SurRender (`SR.DLL`)** (a third-party scene-graph DLL with a software T&L pipeline and an OpenGL/Glide state machine `srGERD`), and
3. **`engine_code` (`st*`, `Gr*`, `GD*`, `Video2`)** (subclasses, vtable hierarchies, and 3dfx Voodoo 256×256 texture workarounds used to bridge SGP and SurRender).

In a big-bang redesign, we **collapse all three into one flat, data-oriented architecture**:

| Legacy Layer / Mechanism | Big-Bang Replacement |
| :--- | :--- |
| **`srGERD` + `srTriMeshPipeline` + `srVertexPipe` + `SDLGPUDevice`** (4-layer rendering stack with OpenGL 1.1 matrix stacks and CPU lighting scratchpads) | **Single `GpuRenderer` class** owning SDL_GPU pipelines, a 3D mesh/particle draw queue, a 2D overlay quad queue, and a CPU ray-picker for world models. |
| **`stSurface2D` (30× 128×128 tile textures) + 8 `VideoOverlayScenes` + `stModelInstance2D` + `PurgeInactiveSceneInstances`** | **Direct 2D Compositor**: One 640×480 streaming GPU texture for the UI canvas, plus a flat per-frame `std::vector<OverlayQuad>` for rotated/blended HUD sprites, fades, tooltips, and the cursor. |
| **`srClass` + `srNode` + `srModel` + `srMeshModel` + `stMeshModel` + `srModelInstance` + `stModelInstance`** (7-class deep polymorphic scene graph) | **Two concrete types**: **`Mesh`** (geometry, compressed frames, skins, vertex lights) and **`MeshInstance`** (transform, active frame/skin, highlight/shadow flags). No generic `srNode` linked-list tree! |
| **`srLight` + `stLight` + `MonsterLight` + `stLightDefinition` + `stParametricLightDefinition` + `stKeyframedLightDefinition`** (6-class hierarchy) | **One concrete `Light` struct** with a `std::variant<std::monostate, ParametricLight, KeyframedLight, MonsterLight>` animator. |
| **`W8GrObject` + `W8Navigator` + `W8GrCycle` $\leftrightarrow$ `W8AnimRepBase` + `W8AnimRep` + `W8EmitterHost` + `*Rep`** (dual parallel inheritance diamond + 10 pure virtual getters) | **Zero entity inheritance**: `Monster`, `Missile`, `SpellVisual`, `Prop`, and `WorldItem` are standalone concrete classes that compose a **`CyclePlayer`** value member and a **`Navigator`** value member. |
| **`W8AIRecord` (`W8PathAI` vs. `W8AIMissile`)** polymorphic tagged union | **`KeyframeTrack`** (concrete spline struct) and **`MissileFlight`** (value member inside `Missile`). |
| **Entangled `W8Octree` $\leftrightarrow$ `W8GameData` $\leftrightarrow$ `W8PathingService`** with mutable member scratchpads and `TestProp` vector-swapping | **`LevelCollision`** (immutable `.oct` + `.wgd` geometry with pure, `const` ray/box/ground queries) and **`NavigationMesh`** (`.wpt` graph + stateless A*/patrol planner + steering). |

---

## 2. The Five Target Subsystems

### Subsystem 1: `canvas/` and `audio/` (Dissolving `src/sgp`)

`src/sgp` is completely removed. Its surviving functionality becomes two self-contained, dependency-free modules:

1. **`canvas/` (CPU 2D Surface & Font Rendering)**
   - Owns `BitmapImage` (`SDLSurfaceOwner`), `SpriteSheet` (STI decoding & ETRLE frames), `Font` (glyph metrics & palette shades), and 16-bit ARGB1555 software blitting (`BlitImage`, `DrawSprite`, `DrawText`, `ShadeTable`).
   - Purely operates on `SDL_Surface&`. Knows nothing about 3D scenes, GPU textures, or global framebuffers.
2. **`audio/` (Unified Sound, 3D Positional Audio, & Lip-Sync)**
   - Absorbs `sgp/soundman.cpp`, `sgp/native/audio.cpp`, `engine_code/stSound3D.h` (extracted from `Spells.cpp`), `engine_code/SoundEvent.cpp`, `engine_code/AmbientSound.cpp`, and `engine_code/mouth_gap.cpp`.
   - **`Sound3D`** is a plain value struct (no `srNode` inheritance!): `{ std::string wave_name; glm::vec3 position; float falloff; int volume; Playback handle; }`.
   - **`AmbientAudioSystem`** owns the level's ambient emitters and random tracks (`W8AmbientSound`, `W8AmbientTrack`) and updates them against the listener's position/orientation each tick.

---

### Subsystem 2: `render/` (Replacing `SurRender` + `st*` + `Video2`)

We replace the entire `src/surrender` library, `Video2.cpp`, `surface2d.cpp`, and all `st*` scene classes with a single, cohesive **`render/`** subsystem designed specifically for Wizardry 8.

#### 2.1 Explicit Scene Lists Instead of a Generic `srNode` Scene Graph
In retail, `srScene` walked a doubly-linked tree of polymorphic `srNode`s (`traverse()` $\rightarrow$ `TraverseInfo::entries` $\rightarrow$ `process()`), using `dynamic_cast` and bitflags (`FLAG_DISABLE`, `FLAG_TERMINATE`, `FLAG_GLOBAL`) to discover lights, fog, clip planes, meshes, particles, and shadows.

In fact, Wizardry 8 only ever renders **four kinds of 3D primitives** in a `WorldScene`:
1. **Static & Animated `MeshInstance`s** (level submeshes, props, items, monsters, missiles, spell visuals, 3D cursor cubes),
2. **`ParticleSystem`s** (`stParticle`: level particles, cycle particles, retired particles),
3. **`GroundShadow`s** (`stGroundShadow`: monster blob shadows projected onto nearby octree floor triangles),
4. **Lighting & Fog State** (`Light`s, ambient color, `FogParams`).

**New `WorldScene` Design**:
```cpp
struct WorldScene {
    Camera3D camera;
    glm::vec3 ambient_light{1.0f};
    glm::vec3 fog_color{0.0f};
    std::optional<FogParams> static_fog;
    std::optional<FogParams> dynamic_fog;

    // Explicit typed collections instead of an untyped srNode tree:
    std::vector<std::unique_ptr<MeshInstance>> static_meshes; // Level geometry
    std::vector<MeshInstance*> dynamic_meshes;                // Props, items, monsters, missiles, spells
    std::vector<ParticleSystem*> particles;                   // Active level & entity particle systems
    std::vector<GroundShadow*> shadows;                       // Active monster ground shadows
    std::vector<Light*> lights;                               // Static, environment, camera & cycle lights
};
```
- **No `srClass`, no `srNode`, no `traverse()`, no `process()`, no `dynamic_cast`**.
- Attaching or detaching an entity's visuals from the world is simply registering/unregistering its typed pointers or `Renderables` view with `WorldScene`.
- Hierarchical attachment (e.g., a monster's spell icon or particle attached to a mapped vertex) is computed explicitly when updating the entity's transform, rather than through lazy `mutable` double-precision `dmat4x3` matrix caches on every node.

#### 2.2 Unified `Mesh` and `MeshInstance`
Merge `srMeshModel` + `stMeshModel` into **`Mesh`**, and `srModelInstance` + `stModelInstance` into **`MeshInstance`**:
- **`Mesh`**:
  - Owns base vertex positions, normals, face indices, plane equations, UV layers, material/texture assignments, and `srShader` flags.
  - Owns optional `std::vector<CompressedFrame>` and an LRU-managed decompressed float cache (`FrameGeometry`).
  - Owns `std::vector<Skin>` (named texture-replacement tables for monster damage stages andblanking) and `automap_polygons` filter.
  - Owns `vertex_lights[2]` (normal vs. automap baked lighting) and `vertex_sunlight`.
  - Owns `std::vector<std::unique_ptr<Mesh>> parts` (multi-material submeshes, replacing the intrusive `head`/`parts` linked list).
- **`MeshInstance`**:
  - Holds `std::shared_ptr<Mesh> mesh`, `glm::mat4 transform`, `uint32_t frame_index`, `float frame_interpolation`, `int skin_index`, `glm::vec3 light_scale`, `std::optional<glm::vec4> highlight_color`, and flags (`visible`, `cast_shadow`, `pickable`, `lighting_baked`, `mirror_x`).

#### 2.3 Unified `Light`
Merge `srLight` + `stLight` + `MonsterLight` + `stLightDefinition` into a single **`Light`** class:
```cpp
using LightAnimator = std::variant<
    std::monostate,
    ParametricLightAnim, // Flicker, oscillate, ramp, color lerp
    KeyframedLightAnim,  // Frame-sum / intensity / color keyframe tables
    MonsterLightAnim     // Two-color cycle and death fade-out
>;

struct Light {
    std::string name;
    glm::vec3 position{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{1.0f}, specular{0.0f};
    float intensity = 1.0f;
    float range = 1500.0f;
    uint32_t group_mask = 1;
    bool enabled = true;
    bool save_marked = false;
    LightAnimator animator;
    std::optional<KeyframeTrack> path;
    W8Prop* attached_prop = nullptr;

    void update(float time_seconds);
};
```

#### 2.4 Direct 2D Compositor (Deleting `stSurface2D`, `stModelInstance2D`, and `VideoOverlayScenes`)
Replace the entire 2D-in-3D overlay scene machinery in `Video2.cpp` and `surface2d.cpp`:
- **`UiCanvasTexture`**: Owns the 640×480 ARGB1555 `SDL_Surface` (`GetFrameBufferSurface()`) and a single 640×480 GPU texture. `InvalidateRegion(rect)` unions dirty bounds; at frame submission, only the dirty sub-rectangle is uploaded to the GPU.
- **`OverlayQuad`**: A plain struct for rotated/alpha/scaled 2D elements (radar map & blips, formation compass, automap markers, fullscreen fades, tooltips, and the mouse cursor):
  ```cpp
  enum class OverlayLayer : uint8_t { PreWorld, UnderUi, OverUi, SquareAspect, Fullscreen, Cursor };

  struct OverlayQuad {
      OverlayLayer layer = OverlayLayer::OverUi;
      std::shared_ptr<Texture> texture; // Null for solid-color quads
      DesignRect screen_rect{};
      glm::vec2 uv_min{0.0f, 0.0f}, uv_max{1.0f, 1.0f};
      float rotation_radians = 0.0f;
      glm::vec4 color{1.0f};            // Tint or solid fill color (supports pulsing glow)
      bool alpha_blend = true;
  };
  ```
- Screens and HUD widgets (radar, formation, automap, tooltips) hold value `OverlayQuad`s or submit them for the frame. **Deleted**: `stSurface2D`, `stModelInstance2D`, `W8ModelInstance2DRenderState`, `VideoOverlayScenes`, `PurgeInactiveSceneInstances`, `CopySurfaceWithBorder`, `Video2DRectToPolygon`, `Video2DRectToSquarePolygon`, `ReleaseObject`.

---

### Subsystem 3: `spatial/` (Unifying `Octree`, `GameData`, `OctBuildTree`, `OctPath`, and `Navigator`)

All spatial indexing, collision detection, visibility (PVS), raycasting, and pathfinding are unified into `spatial/` with **strict separation between immutable level data, dynamic entity registration, and per-query scratch state**.

#### 3.1 `LevelSpatialIndex` (Merges `W8Octree` + `W8GameData` Collision + `W8OctBuildTree`)
Instead of `W8Octree` and `W8GameData` holding raw back-pointers to each other and sharing global mutable scratch arrays:
1. **Immutable Geometry & PVS Data (`const` after load)**:
   - Octree branches, leaves, leaf lookup grid, region volumes, region-to-mesh PVS links (`m_region_links`, `m_particle_links`, `m_prop_links`),
   - Collision surfaces (`std::vector<W8GDSurface>`) and vertices (`std::vector<glm::vec3>`),
   - Switch interfaces (`W8GDInterface`, `W8GDInterfaceState`, `m_piCondPolys`),
   - `W8OctBuildTree` (the dynamic triangle collision octree built from `W8GameData` surfaces).
2. **Dynamic Spatial Registry**:
   - `W8OctreeObjectRegistry` (tracks which cells hold dynamic props, waypoints, monsters, and navigators),
   - Active trigger plane bits (`pending_trigger_bits`, `active_trigger_bits`),
   - Current region visibility bitsets (`m_current_regions`, `m_visible_props`, `m_visible_particles`).
3. **Pure, Stateless Ray & Box Queries (Zero Member Scratchpads)**:
   - Delete `m_aulGDObjs`, `m_visited_polygon_bits`, `m_visited_object_bits`, `last_hit_surface`, `m_trace_skip_flag`, and `trace_flag4_gate` from the class.
   - All raycasts and box queries allocate a lightweight `TraceScratch` on the stack and return a typed value:
     ```cpp
     struct TraceHit {
         glm::vec3 point{0.0f};
         float distance = std::numeric_limits<float>::max();
         int surface_index = -1;
         int prop_id = -1;
         int location_id = -1;
         const W8GDSurface* surface = nullptr;
     };
     ```
   - **No `TestProp` vector swapping**: `traceSurfaces(std::span<const W8GDSurface>, std::span<const glm::vec3>, const Ray&, TraceFilter)` is a pure function called identically for world geometry and for prop-local geometry.
   - **Delete Dead Region-Link Builder**: Remove `BuildRegionLinks`, `SampleRegionLinks`, `SaveRegionLinks`, `SavePoints`, `CountBadRegionMeshLinks`, and the global `g_render_unlit` / `g_render_cull_front` / `g_render_untextured` switches from `Octree.cpp`.

#### 3.2 `NavigationMesh` and `Navigator` (Clean Split of `OctPath.cpp` and `Navigator.cpp`)
1. **`NavigationMesh` (`spatial/nav_mesh.*`)**:
   - Owns the `.wpt` waypoint graph (`m_waypoints`, `m_pEdges`), cell hash (`m_pPathValues`), and conditional prop path tables (`m_pCondPaths`).
   - Provides **stateless path planning**: `findPath(...)`, `buildPatrolPath(...)`, and `planMovement(...)` use a local `PathSearchScratch` object instead of storing `m_search_nodes`, `path_heap`, `m_patrol_*`, and `m_trace_*` as persistent members of the service.
2. **`Navigator` & `Steering` (`spatial/navigator.*`)**:
   - Merges `W8Navigator`, `W8NavigatorMovementState`, `W8NavigatorAttachment`, and `W8PathParameters` (the local boids/obstacle-avoidance steering context in `OctPath.cpp`) into one cohesive navigation module.
   - Deletes `W8NavigatorOwned0A0` and the secondary-vtable glue.

---

### Subsystem 4: `world/` (Consolidating `W8World`, Level Loading, Environment, and Camera)

#### 4.1 Structured `World` Container
Replace the 70-field flat `W8World` struct (`layouts/world.h`) with a structured **`World`** class composed of domain sub-objects:
```cpp
class World {
public:
    const int level_id;
    const std::string source_path;

    WorldScene scene;                 // 3D camera, lights, fog, mesh/particle/shadow render lists
    LevelSpatialIndex spatial;        // Octree, collision surfaces, PVS, dynamic cell registry
    std::unique_ptr<NavigationMesh> nav_mesh; // Present on octree levels
    EnvironmentState environment;     // Day/night color ramps, sun/moon orbit, sky fade state
    AmbientAudioSystem audio;         // Positional & region-bounded ambient sound emitters
    W8AutomapCells automap;           // Visited/lit automap grid

    // Gameplay Entities owned by the world:
    W8LevelPopulation population;     // Monster groups, roster, species cache, world item records
    W8EncounterState encounters;      // Monster generators and encounter budget
    std::vector<std::unique_ptr<Monster>> monsters;
    std::vector<Monster*> active_monsters;
    std::vector<std::unique_ptr<WorldItemEntity>> item_entities;
    std::vector<std::unique_ptr<Prop>> props;
    std::vector<std::unique_ptr<Trigger>> triggers;
    std::vector<TriggerEvent*> timed_events;
    std::vector<std::unique_ptr<TriggerEvent>> standalone_events;
    std::vector<std::unique_ptr<Missile>> missiles;
    std::vector<std::unique_ptr<SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<CameraPath>> camera_paths;
    std::vector<W8NamedPosition> named_positions;
    std::unique_ptr<WorldCursor> cursor;
};
```
Destruction order is guaranteed naturally by member declaration order (entities are declared last, so they are destroyed first, automatically detaching from `audio`, `nav_mesh`, `spatial`, and `scene` before those are destroyed).

#### 4.2 Unified `PartyCamera` (`world/party_camera.*`)
Merge `GDCamera.cpp`, `Camera.cpp`, `3dapi.cpp` (camera sway & camera-state sync), and `GameData.cpp` (`W8LevelDataRecord` party physics, gravity, slope sliding, AABB clamping, and footstep triggers) into **`PartyCamera`**:
- Owns `GDCamera` orientation/transition state, `W8LevelDataRecord` party motion physics, `W8EnvironRecord` active physics environment, and camera sway state.
- Replaces all `void* angle, void* pitch` and `float[6]` runtime camera APIs with `struct CameraPose { glm::vec3 position; float yaw; float pitch; };`.

#### 4.3 Unified Level Loader (`world/level_loader.*` & `world/level_session.*`)
- **`level_loader.*`**: Combines `LoadWorld` (`3dapi.cpp`), `ReadLevel.cpp`, `ReadGameData` (`GDFileIO.cpp`), `ReadMesh.cpp`, and `OctSubMesh.cpp` into one cohesive level-deserialization module that returns a fully constructed `std::unique_ptr<World>`.
- **`level_session.*`**: Combines `Levels.cpp` and `UpdateWorlds` (`3dapi.cpp`) — manages the active `World` and `SkyWorld`, level transitions (`LoadLevel`, `LeaveLevel`), save snapshots, and the per-frame world update tick.

---

### Subsystem 5: `entities/` (Flattening the `GrObject` / `GrCycle` / `AnimRep` Diamond)

We **completely delete**:
- `W8GrObject`, `W8AnimRepBase`, `W8AnimRep`, `W8EmitterHost`, `W8GrCycle`,
- `W8ItemRep`, `W8PropRepresentation`, `GDProp`, `W8MissileRep`, `W8SpellEmitterHost`, `W8MonsterRep`,
- `W8AIRecord`,
- `W8TriggerActionData` polymorphic hierarchy (`W8EnvironmentTriggerActionData`, `W8StringTriggerActionData`, `W8DoorTriggerActionData`).

In their place, we introduce **two small reusable animation components** and **five flat, self-contained entity classes**:

#### 5.1 Reusable Animation Components (`entities/animation.*`)
1. **`KeyframeTrack`** (formerly `W8PathAI`, without `W8AIRecord` base class):
   - Evaluates keyframed positions, rotations, and scales over time or normalized distance, and applies them to a transform or `Light`/`Camera3D`.
2. **`CyclePlayer`** (replaces `W8AnimRep` + `W8EmitterHost` + `W8GrCycle`):
   - A concrete value component embedded in `Monster`, `Missile`, and `SpellVisual`.
   - Holds transform (`location`, `local_location`, `parent_location`, `rotation`, `scale`, `highlight_colour`), frame playback state (`current_cycle`, `subcycle`, `first_frame`, `last_frame`, `frame_direction`, `frame_method`, `behaviour`, `timer`, `frame_fraction`, `lod`), and scene attachments (`MeshInstance*`, `std::vector<ParticleAttachment>`, `std::deque<ShakeDefinition>`, `std::vector<std::unique_ptr<Light>>*`, `std::unique_ptr<GroundShadow>`).
   - Driven by direct method calls (`tickAnimation(const W8AnimObj&, float scale)`, `selectLod(const W8AnimObj&, glm::vec3 eye)`, `updateAttachments(World&)`), eliminating all 10 pure-virtual bounce methods on `W8GrCycle`.

#### 5.2 Flat, Concrete Entity Classes
1. **`WorldItemEntity` (`entities/item.*`)** — merges `W8Item` + `W8ItemRep`:
   - Owns `std::unique_ptr<MeshInstance> mesh`, local bounds, `pulse_level`, `entity_flags`, `Trigger*`, and `uint64_t countdown`.
2. **`Prop` (`entities/prop.*`)** — merges `W8Prop` + `W8PropRepresentation` + `GDProp`:
   - Owns its `std::optional<W8AnimObj> animation`, `std::vector<W8PropAnimationSegment> slots`, `W8GameTimer timer`, `Trigger*`, and its collision/pathing geometry (`m_pGDSurfaces`, `m_pVertices`, `m_path_edges`, `m_path_waypoints`, `m_supported_items`). No separate `Rep()` or `m_gd_prop` heap allocation, and no cross-class `friend` web.
3. **`Missile` (`entities/missile.*`)** — merges `W8Missile` + `W8MissileRep` + `W8AIMissile`:
   - Owns `CyclePlayer visual`, `Navigator nav`, `EmitterResources<2> resources`, `MissileFlightAI flight_ai`, and spell/combat impact payload (`W8SpellEffectDefinition`, `W8TargetSource`, `W8CombatSlot`, `W8SpellEffectResult`).
4. **`SpellVisual` (`entities/spell_visual.*`)** — merges `W8SpellVisual` + `W8SpellEmitterHost`:
   - Owns `CyclePlayer visual`, `Navigator nav`, `EmitterResources<28> resources`, `W8SpellVisualMode mode`, and target/anchor fields.
   - Spell database queries (`InitializeSpellDatabase`, `GetSpellTargetType`, `CanSpellBackfire`, etc.) move to `gameplay/spells.cpp`.
5. **`Monster` (`entities/monster_*.*`)** — merges `W8Monster` + `W8MonsterRep`:
   - Owns `CyclePlayer visual`, `Navigator nav`, `std::array<CycleSlot, W8_MONSTER_CYCLE_COUNT> cycles` (each holding `animations`, `light_lists`, `animation_scales`), `std::unique_ptr<Light> monster_light`, attached icon `WorldItemEntity`s, `stScript` state, and mouth-gap talking state.
   - Delete all ~50 `Monster*()` C-style wrapper functions in `Monster.h`; callers invoke `monster->...` methods directly.
   - Organized into three focused implementation files:
     - `monster_loader.cpp` (`.mon` / `.mls` script parsing, skin/damage stage setup),
     - `monster_script.cpp` (`stScript` execution and order state machine),
     - `monster_actor.cpp` (per-frame update, fade, talking/mouth-gap, sight checks, attachment icons).
6. **`Trigger` (`entities/trigger_*.*`)**:
   - Drops `srClass` base class.
   - Replaces `std::unique_ptr<W8TriggerActionData>` with `std::variant<std::monostate, EnvironmentTriggerData, StringTriggerData, DoorTriggerData>`.
   - Organized into `trigger_io.cpp` (`TRES` chunk serialization), `trigger_locks.cpp` (`W8LockState`), and `trigger_actions.cpp` (`Trigger::Run`).

---

## 3. Complete File Mapping (Old $\rightarrow$ New)

| Legacy Files (`sgp/`, `surrender/`, `engine_code/`) | New Target Location | Architectural Change |
| :--- | :--- | :--- |
| `sgp/himage.*`, `sgp/image.*`, `sgp/image_assets.*`, `sgp/sprite.*`, `sgp/Font.*`, `sgp/shading.*` | `src/canvas/` | Pure 16-bit CPU surface, STI sprite, and font library. |
| `sgp/soundman.*`, `sgp/native/audio.cpp`, `engine_code/stSound3D.h`, `engine_code/SoundEvent.*`, `engine_code/AmbientSound.*`, `engine_code/mouth_gap.*` | `src/audio/` | Unified audio service + 3D positional audio (no `srNode`) + ambient system + lip-sync. |
| `sgp/input.*`, `sgp/native/video.cpp`, `sgp/native/main.cpp`, `sgp/Random.*`, `sgp/timer.h`, `engine_code/game_timer.*`, `engine_code/GameTimeAccumulator.*`, `engine_code/IntervalGate.*` | `src/platform/` | Window, SDL input events, RNG, and game timers/clocks. |
| `surrender/devices/sdl_gpu/*`, `surrender/gerd*.cpp`, `surrender/renderer.cpp`, `surrender/vertex_pipe.cpp`, `surrender/triangle_culler.cpp`, `surrender/illuminator.cpp`, `surrender/clipplane.cpp`, `surrender/fog.cpp`, `surrender/camera.cpp`, `surrender/scene.cpp`, `surrender/node.cpp` | `src/render/pipeline/` | Unified GPU renderer, `WorldScene` typed draw lists, frustum culling, and ray-picking. (`srClass` & `srNode` tree deleted). |
| `surrender/texture.cpp`, `surrender/image_*.cpp`, `surrender/huffman.cpp`, `surrender/material.cpp`, `surrender/mesh_model.cpp`, `surrender/model_instance.cpp`, `engine_code/stMeshModel.*`, `engine_code/stModelInstance.*`, `engine_code/stTextureFile.*`, `engine_code/stTextureAnim.*`, `engine_code/materials.*` | `src/render/resources/` | Unified `Mesh` (`srMeshModel` + `stMeshModel`), `MeshInstance` (`srModelInstance` + `stModelInstance`), `Material`, and `Texture` (`srTexture` + `stTexture*`). |
| `surrender/light.cpp`, `engine_code/stLight.*`, `engine_code/MonsterLight.*`, `engine_code/stParticle.*`, `engine_code/stGroundShadow.*` | `src/render/effects/` | Unified `Light` (with `std::variant` animator), `ParticleSystem`, and `GroundShadow`. |
| `engine_code/Video2.*`, `wiz8/surface2d.*`, `engine_code/Quality.*` | `src/render/compositor/` | Single 640×480 streaming UI texture + `OverlayQuad` list (`stSurface2D`, `stModelInstance2D`, and 8 `VideoOverlayScenes` deleted). |
| `engine_code/Octree.*`, `engine_code/OctBuildTree.*`, `engine_code/GameData.*` (collision/surfaces), `engine_code/UpdateMesh.*`, `engine_code/quad.*`, `engine_code/PolyPick.*` | `src/spatial/level_spatial.*` | `LevelSpatialIndex`: const, stateless ray/box/ground traces returning `TraceHit` values; dead region-link builder deleted. |
| `engine_code/OctPath.*`, `engine_code/Navigator.*` | `src/spatial/navigation.*` | `NavigationMesh` (`.wpt` graph + stateless A*/patrol search) + `Navigator` (movement & boids steering). |
| `engine_code/World.h`, `layouts/world.h`, `engine_code/3d.*`, `engine_code/3dapi.*`, `engine_code/world_selection.cpp`, `engine_code/Levels.*`, `engine_code/ReadLevel.*`, `engine_code/ReadMesh.*`, `engine_code/OctSubMesh.*`, `engine_code/GDFileIO.*`, `engine_code/Level.h` | `src/world/` | Structured `World` container, `level_loader.*`, and `level_session.*`. |
| `engine_code/GDCamera.*`, `engine_code/Camera.*`, `engine_code/camera_transition_state.cpp`, `engine_code/GameData.*` (camera/party motion) | `src/world/party_camera.*` | Unified `PartyCamera` (orientation, transitions, sway, cinematic paths, party physics, footsteps). |
| `engine_code/Environment.*`, `engine_code/Cursor3d.*`, `engine_code/stCube.*` | `src/world/environment.*`, `src/world/world_cursor.*` | Day/night lighting/fog/sky controller and 3D world targeting cursor. |
| `engine_code/GrObject.*`, `engine_code/AnimRep.*`, `engine_code/Emitter.h`, `engine_code/emitter_*.h`, `engine_code/GrCycle.*`, `engine_code/AnimObj.*`, `engine_code/AniMesh.*`, `engine_code/PathAI.*` | `src/entities/animation.*` | `KeyframeTrack`, `AnimatedMesh`, and concrete `CyclePlayer` component (`GrObject`/`AnimRep`/`EmitterHost`/`GrCycle` hierarchy deleted). |
| `engine_code/Prop.*`, `engine_code/GDProp.*`, `engine_code/Item.*`, `engine_code/Missile.*`, `engine_code/Spells.*`, `engine_code/SpellVisual.h`, `engine_code/SpellEmitterHost.h`, `engine_code/Trigger.*`, `engine_code/Monster.*`, `engine_code/stScript.*`, `engine_code/MonGen.*` | `src/entities/` | Flat, concrete `Prop`, `WorldItemEntity`, `Missile`, `SpellVisual`, `Trigger`, and `Monster` classes. |

---

## 4. Big-Bang Execution Sequence

To execute this cleanly in a dedicated worktree without leaving transitional shims:

1. **Step 1 — Build the Foundation Modules (`platform/`, `canvas/`, `audio/`)**:
   - Move SGP 2D/font/sprite files to `src/canvas/`, timers/input/RNG to `src/platform/`, and unify `audio/` (`soundman`, `Sound3D`, `SoundEvent`, `AmbientSound`, `mouth_gap`).
2. **Step 2 — Replace `SurRender` + `st*` + `Video2` with `render/`**:
   - Merge `srMeshModel`+`stMeshModel` $\rightarrow$ `Mesh`, `srModelInstance`+`stModelInstance` $\rightarrow$ `MeshInstance`, `srLight`+`stLight`+`MonsterLight` $\rightarrow$ `Light`.
   - Replace `srNode` scene trees with `WorldScene` typed draw lists.
   - Replace `stSurface2D` + `VideoOverlayScenes` + `stModelInstance2D` with the single 640×480 streaming UI texture and `OverlayQuad` compositor in `render/compositor/`.
3. **Step 3 — Flatten Entities & Animation (`entities/`)**:
   - Replace `W8AIRecord` with `KeyframeTrack` and inline `MissileFlightAI`.
   - Replace `W8GrObject` / `W8GrCycle` / `W8AnimRep` / `W8EmitterHost` and all `*Rep` classes with `CyclePlayer` composition inside flat `Prop`, `WorldItemEntity`, `Missile`, `SpellVisual`, and `Monster` classes.
   - Convert `Trigger` payloads to `std::variant` and remove the ~50 `Monster*()` C wrappers.
4. **Step 4 — Unify Spatial, Navigation, Camera, and World (`spatial/`, `world/`)**:
   - Make `LevelSpatialIndex` (`Octree` + `GameData` collision) and `NavigationMesh` (`OctPath`) const and stateless by extracting `TraceScratch` and `PathSearchScratch`, and deleting the `TestProp` vector-swap hack and dead region-link builder.
   - Merge `GDCamera`, `Camera`, and `W8LevelDataRecord` party motion into `PartyCamera`.
   - Restructure `W8World` into `World` and consolidate all `.lvl`/`.pvl`/`.oct`/`.wgd` readers into `world/level_loader.cpp`.
5. **Step 5 — Update Callers & Verify**:
   - Update `local_code/`, `local_screens/`, `dialog_code/`, `level_specific_code/`, and `tests/` to use the new headers and direct entity/world APIs.
   - Run full CTest unit suite and `WIZ8_ASSET_ROOT` runtime scenarios (`-L runtime`) under ASan/UBSan to verify 100% behavioral and save-format parity.

---

## 5. Implementation Status

The former #371 / #372 / #374 stack is consolidated into #374 (`engine-modernization-audio-3d`), rebased directly onto `main`. The consolidated PR includes this complete plan, all three implementation batches, and conflict resolutions that preserve the newer native enum, surface, assertion, and world-item APIs. #371 and #372 are superseded.

| Step | Status | Where |
| :--- | :--- | :--- |
| 1. Foundations (`canvas/`, `platform/`, `audio/`) | Relocation done: `src/sgp` removed, modules under `src/canvas`, `src/platform`, `src/audio`. 3D sound extracted to `audio/sound3d` as `Sound3D`. | #374 |
| 1. Foundations, remaining | `Sound3D` still uses a global borrowed index (`g_sound3d_instances`) and `Retain3DSound`. `AmbientAudioSystem` owner, `SoundEvent`, and `mouth_gap` are not yet value types. | — |
| 2. `render/` | Not started. `srNode` and `stSurface2D` remain. | — |
| 3. `entities/` | Done: `Prop` merged with its animation representation; `Trigger` payloads are a `std::variant`; `W8PathAI` no longer derives from `W8AIRecord`; the 37 `Monster*()` wrappers are `W8Monster` members or removed (no null guards at call sites; nullable callers remain review work); `W8MissileRep`, `W8MonsterRep` and `W8SpellEmitterHost` are merged into `W8Missile`, `W8Monster` and `W8SpellVisual`. The shared `W8GrObject`, `W8AnimRepBase`, `W8AnimRep`, and `W8EmitterHost` bases are deleted: animation transforms, playback, LOD, path, and sound-event ownership now live directly in `W8GrCycle`; `Prop` and `WorldItemEntity` have no entity base. The representation getter and its virtual bounce are gone. Clone paths retain independent animations, lights, sound events, and AI. Old numeric APST records still resolve through an explicitly named legacy prop-save key and the original construction sequence; other entities no longer store unused IDs. | #374 |
| 3, remaining | `WorldItemEntity` already owns its representation directly. Extract a concrete `CyclePlayer` value from `W8GrCycle`, replace its animation virtuals with explicit component inputs, and compose navigation in the entity owners. `GDProp` remains separately owned by `Prop`. | — |
| 4. `spatial/`, `world/` | Path planning: the visited and marked node sets, the open-set heap, the search-node array, and the patrol walk state are per-search values now, not members. Still members: the trace configuration that `ConfigureMovementSearch` sets for later calls, which needs an explicit parameter object before it can move. Started: the dead region-link builder (`BuildRegionLinks`, `SampleRegionLinks`, `SaveRegionLinks`, `SavePoints`, `CountBadRegionMeshLinks`, `ValidateRegionMeshLinks`), its dev-only level-link loop, and the debug render switches (`g_render_unlit`, `g_render_cull_front`, `g_render_untextured`) are deleted. The `.rlk` files still ship and are read. The octree's per-query scratch (`OctreeScratch`: keys, count, and dedupe sets) replaces the member buffers. The flag-4 gate is a parameter, and the skip flag is gone. The last trace's surface now travels on the trace record and the ground probe, not on `GameData`; a probe that makes no ray test reports no surface instead of a stale one. The region-link recorder, the octree's registry cursor globals, the prop id on the trace (now returned on the ground probe), and six unreferenced navigator, pathing, and monster functions are removed. Remaining: `LevelSpatialIndex` is not yet const, because the visibility sets (`m_current_regions` and friends) are written during queries. | — |
| 5. Verification | The consolidated implementation builds locally with Clang 23 and warnings as errors. Release and AddressSanitizer builds each pass all 227 CTest cases, including all 14 retail-asset runtime scenarios, emitter clone lifetime, and named/numeric prop saves. Static analysis and validation details are recorded below. CI remains a separate check on the published commit. | #374 |

Verification and remaining review work:

- The earlier Monster wrapper batch removed the null guards that the wrappers provided. Nullable callers still need explicit review before the stack merges; the selected runtime scenarios do not establish that every caller is non-null.
- The previous CI compile/analyzer failures (shadowed monster members, dead region-link constants, and an incomplete ground-shadow model type) are fixed in the consolidated implementation.

Validation before consolidation (2026-10-19; rebase verification follows separately):

- Release build: `ninja -C build/native-rel -j 8`, Clang 23, warnings as errors.
- Release and AddressSanitizer builds: all 210 non-runtime CTest cases passed.
- Both builds passed the five installed-asset scenarios `resource-lifecycle`,
  `camp-spells`, `combat`, `quick-save`, and `load-game` against prepared GOG assets.
- The ASan runs used `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`,
  matching CI's software Vulkan selection, with leak detection enabled and no
  suppressions. Initial runs on the default host driver reported D-Bus leaks;
  the software-driver run also passed the GPU rendering fixture.
- Targeted `clang-tidy` passed for all 14 changed C++ translation units using
  the repository configuration; the obsolete `PathAI.cpp` animation include
  was removed after its first analysis finding. Changed C++ files passed
  `clang-format --dry-run --Werror`; `git diff --check` passed.
- These are bounded regression checks, not a claim of complete gameplay,
  save-format, or platform parity. Remote CI is reported separately on #374.

Consolidation and rebase validation (2026-10-19):

- #374 now targets `main` directly; the native GitHub stack was dissolved and
  #371 / #372 closed. The original local tip is retained at
  `backup/engine-stack-before-rebase-20261019`.
- Rebase conflict resolutions retain `main`'s typed enums, simplified native
  surfaces, flat world-item groups, and assertion call-site diagnostics.
- Clang 23 release build with warnings as errors passed. All 227 CTest cases
  passed, including all 14 installed-asset runtime scenarios.
- The AddressSanitizer build also passed all 227 CTest cases with leak
  detection enabled, no suppressions, and CI's software Vulkan driver.
- A full 285-translation-unit clang-tidy scan found one duplicate timer include
  and two obsolete includes after conflict resolution. They were removed and
  every affected failing translation unit was rechecked. Formatting and
  whitespace checks passed. Remote CI must pass on the consolidated head
  before merge.
