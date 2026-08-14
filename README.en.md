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
4. Play

This creates and assigns a Post-Process Anim Blueprint named `ABP_<MeshName>_MmdPhysics`
next to the skeletal mesh.

On startup the plugin cross-checks `extras.mmd` bone positions against the skeleton's reference pose
and logs an actionable error if the import convention does not match.

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
| UE 5.5 | Development and verification target. 8 automated tests green |
| UE 5.6 | Source-level compatibility only. **Not verified on a real install** |

## Known limitations

- **The model is assumed to be at the world origin.** Simulation runs in component space, so there is
  no momentum carry-over when the character moves. This limitation is inherited from the Unity version.
- **Outlines (edges) are not supported yet.** `edgeColor` / `edgeSize` are stored on the material
  instances, so they can be used by a later implementation.
- **Materials are Unlit.** This matches MMD, which exports with `KHR_materials_unlit`; shading comes
  from a light direction parameter (`LightDir` on `M_MmdToon`) plus the toon ramp. The trade-off is
  that they do not respond to UE scene lights or Lumen.
- **Shared toons (`toon01`..`toon10`) are not bundled** — they are not part of the model either.
  Import them into the project yourself; they are located by name from anywhere in `/Game`.
- `sphereMode: 3` (sub-texture), `ambient` and `specular` are not supported
  (the Unity version does not support ambient/specular either).
- **Hitbox generation (the Unity version's step 3) is out of scope.**
- `SoftBody` (PMX 2.1) is not ported — the source builder does not use it either.
- The `.pmx` direct-read verification path (`PmxReader`) is not ported.
- Small jitter at rest is inherited from the source implementation.

## License

MIT License. See [LICENSE](LICENSE).
No model data or shared toon textures are bundled.
