# mmd2gltf UE5 Physics Importer

A **C++ editor plugin for Unreal Engine 5** that reads MMD-specific physics data
(rigid bodies and joints) from the `extras.mmd` block of a `.glb` produced by
[`mmd2gltf-gui`](https://github.com/masaka1024/mmd2gltf-gui), and drives the bones of a
UE skeletal mesh with it.

This is the UE5 counterpart of
[`mmd2gltf-unity-physics-importer`](https://github.com/masaka1024/mmd2gltf-unity-physics-importer).
Unofficial, independent personal project.

Japanese: [README.md](README.md)

---

## Highlights

### It does not use UE's Chaos PhysicsAsset

Like the Unity version, this plugin **bundles a Bullet-compatible physics engine** and drives
bones directly. It deliberately does not map MMD physics onto UE's `PhysicsAsset`, because:

- UE constraints only expose **symmetric** Swing1 / Swing2 / Twist limits and cannot represent
  MMD's **per-axis asymmetric** rotation limits (`rot_min` / `rot_max`)
- `FLinearConstraint::Limit` is a single scalar shared by all enabled axes, so MMD's independent
  XYZ translation limits cannot be represented either
- The Unity project already built and then abandoned a PhysX-based implementation
  (importer went from 3,541 lines with 49 tuning sliders down to 1,160 lines with none)

### Bit-exact with the source implementation

The engine is a 1:1 translation of the Unity version's C# into C++. An automated test verifies that
after **300 frames on a real model** (117 rigid bodies, 165 joints), every body's position and
rotation matches the C# original **exactly**.

See [docs/porting_notes.md](docs/porting_notes.md) for details, including the floating-point mode
setting that was required to achieve this.

---

## Requirements

- **Unreal Engine 5.5** (5.6 has source-level compatibility branches only — see "Supported versions")
- Visual Studio 2022 with the "Desktop development with C++" workload and a Windows SDK
- A `.glb` produced by `mmd2gltf-gui` — output from generic glTF exporters will not work
  (no `extras.mmd`)

## Installation

See [INSTALL.md](INSTALL.md).

## Usage

1. Import the `.glb` with **UE's standard Interchange glTF importer** (the default path).
   glTFRuntime, the deprecated legacy GLTFImporter, or an FBX detour will not match the coordinate system.
2. Open **Tools → MMD Physics インポーター**
3. Pick the skeletal mesh and the `.glb`, then press **"1. Wire / Re-wire Physics"**
4. Then press **"2. Convert materials to MMD toon"**
5. Play

Step 1 creates and assigns a Post-Process Anim Blueprint named `ABP_<MeshName>_MmdPhysics`
next to the skeletal mesh.

On startup the plugin cross-checks `extras.mmd` bone positions against the skeleton's reference pose
and logs an actionable error if the import convention does not match.

### Troubleshooting

| Symptom | What to check |
|---|---|
| Hair/skirt motion looks coarse or judders | On the MMD Physics node, **`FixedTimeStep` should be `1/60` (0.01667)** and **`SubSteps` 2**. At `1/30` the number of internal steps per frame alternates 0,1,0,1,1,... so the real-time update interval is uneven. To step finer, raise `SubSteps`, not `FixedTimeStep` (see [docs/porting_notes.md](docs/porting_notes.md)) |
| Motion is ~3x faster than MMD | `Gravity` must be 98 (PMX units). It is unrelated to UE world gravity |
| A semi-transparent material has hard, jagged edges | Its material instance should have `M_MmdToonTranslucent` as its parent. If not, the `.glb` material's `alphaMode` is probably not `BLEND` |
| Translucent materials are drawn in the wrong order | Order follows the material slot order. Fix the material order in the model, or set `TranslucencySortPriority` on the skeletal mesh component in the level (that only orders the whole model against other actors) |
| Materials render grey | Look for `[MmdPhysics] マテリアルのコンパイルエラー` in the output log |
| The log reports the physics went NaN | Raise `SubSteps`. Happens on models with very stiff springs or very light bodies |

---

## How it works

Physics runs in **PMX-native coordinates and units** all the way through; conversion happens only at
the boundary with UE, in a single place (`FMmdUeSpace`).

```
position   UE = (px, -pz, py) x UnitScale x 100
rotation   UE = (qx, -qz, qy, qw)
```

This map has determinant +1 — it is a pure rotation (+90 degrees about X), not a mirror, because the
handedness flips twice on the way (PMX left-handed → glTF right-handed → UE left-handed).
As a result, MMD joint angle limits can be passed through unchanged; no mirroring of
lower/upper bounds is needed.

Derivation and measured validation: [docs/coordinate_transform.md](docs/coordinate_transform.md).

---

## Supported versions

| | Status |
|---|---|
| UE 5.5 | Development and verification target. 10 automated tests green |
| UE 5.6 | Source-level compatibility only. **Not verified on a real install** |

## Known limitations

- **The model is assumed to be at the world origin.** Simulation runs in component space, so there is
  no momentum carry-over when the character moves. This limitation is inherited from the Unity version.
- **Outlines (edges) are not supported yet.** `edgeColor` / `edgeSize` are stored on the material
  instances, so they can be used by a later implementation.
- **Materials are Unlit.** This matches MMD, which exports with `KHR_materials_unlit`; shading comes
  from a light direction parameter (`LightDir` on `M_MmdToon` / `M_MmdToonTranslucent`) plus the toon
  ramp. The trade-off is that they do not respond to UE scene lights or Lumen.
- **Shared toons (`toon01`..`toon10`) are not bundled** — they are not part of the model either.
  Import them into the project yourself; they are located by name from anywhere in `/Game`.
- **Ordering between translucent materials follows the material (slot) order.** UE's translucent sort
  key is `Priority → Distance → section order`, and sections of one skeletal mesh tie on the first two,
  so MMD's material order is reproduced as-is. However **`TranslucencySortPriority` is a
  `UPrimitiveComponent` property and cannot be set per slot**, so the Unity version's per-material
  `renderQueue = 3000 + slotIdx` nudging has no equivalent here.
- **Sorting *within* a translucent material (the Unity version's lilToon TwoPass) is not reproduced**,
  because UE translucent materials have no depth-write prepass. This only bites when a single
  `alphaMode=BLEND` material overlaps itself (e.g. see-through hair authored as BLEND). MMD hair, skin
  and clothing are normally `MASK`, so they render in the opaque pass and are unaffected.
- **`origTexture` in `extras.mmd` (the un-prebaked texture) is not supported.** The Unity version
  promotes such materials from `MASK` to translucent and uses the un-prebaked image to reproduce MMD's
  soft eyebrows and see-through hair. This port does not extract textures from the GLB binary, so it
  uses `alphaMode` as-is.
- `sphereMode: 3` (sub-texture), `ambient` and `specular` are not supported
  (the Unity version does not support ambient/specular either).
- **Hitbox generation (the Unity version's step 3) is out of scope.**
- `SoftBody` (PMX 2.1) is not ported — the source builder does not use it either.
- The `.pmx` direct-read verification path (`PmxReader`) is not ported.
- Small jitter at rest is inherited from the source implementation.

## License

MIT License. See [LICENSE](LICENSE).
No model data or shared toon textures are bundled.
