# mmd2gltf UE5 Physics Importer

A **C++ editor plugin for Unreal Engine 5** that reads MMD-specific physics data
(rigid bodies and joints) from the `extras.mmd` block of a `.glb` produced by
[`mmd2gltf-gui`](https://github.com/masaka1024/mmd2gltf-gui), and drives the bones of a
UE skeletal mesh with it.

This is the UE5 counterpart of
[`mmd2gltf-unity-physics-importer`](https://github.com/masaka1024/mmd2gltf-unity-physics-importer).

> **This is an unofficial, independent personal project.**
> It is not affiliated with, endorsed by, or supported by MikuMikuDance (Yu Higuchi), PmxEditor
> (Kyokuhoku-P), Bullet Physics, Epic Games, or the authors of any model or motion. "MMD" and "PMX"
> are used only to refer to the specifications and to the behaviour being compared against.
> Matching that behaviour is a goal, not a guarantee.

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

### The physics engine is bit-exact with the source implementation

The engine is a 1:1 translation of the Unity version's C# into C++. An automated test verifies that
after **300 frames on a real model** (117 rigid bodies, 165 joints), every body's position and
rotation matches the C# original **exactly**.

★**What matches is the engine core (`MmdPhysicsCore`). Playback behaviour deliberately differs from
the C# version in three places.**

| Difference | Why |
|---|---|
| `PmxPhysicsBuilder::Align` returns the **raw rigid-body pose** for mode1 (bone-following) bones | MMD's [physics + bone alignment] mode is relative to the parent bone's **actual** current pose. Rebuilding the parent from bind lengths produces two different chains, so the tip of a long chain stretches and shrinks (the C# version has the same flaw). Set `bAlignBonePositions=true` for the original behaviour |
| Node defaults `JointVelocityIterations=40` / `JointMaxCorrectionVel=30` | On a 13-link chain the joint solve never reaches the tip; the residual accumulates every frame and never closes. **The two only work as a pair** — either one alone makes things worse |
| `TeleportResetThreshold` (default 3 PMX units = 24 cm/frame) | At an animation loop point the pose jump becomes a kinematic velocity that sweeps the chain aside. When a jump is detected, the same re-alignment used at startup is applied |

The **engine's core defaults are all 0, i.e. bit-identical to the original**, so the parity above
still holds. See [docs/porting_notes.md](docs/porting_notes.md) for details, including the
floating-point mode setting that was required to achieve this.

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
4. Then press **"2. Convert Materials to MMD Toon"**
5. Then press **"3. Build Actor (body + animation + outline)"**
6. Drop the resulting `BP_<MeshName>` into the level and play

Step 1 creates and assigns a Post-Process Anim Blueprint named `ABP_<MeshName>_MmdPhysics`
next to the skeletal mesh.

Step 3 creates a Blueprint actor named `BP_<MeshName>`. Just placing it in the level gets you
physics, materials, outlines, the hair's second pass and the motion all working. **Run it after
step 2**, since it decides what is needed by looking at the converted materials.

```
BP_<MeshName>
└─ Mesh (skeletal mesh) … physics comes from the Post-Process AnimBP; the motion is assigned here too
   ├─ SoftPass  … the hair's second pass (soft tips). Added only if some material needs it
   └─ Outline   … outlines. Added only if some material draws one
```

### How motion (VMD) is handled

`mmd2gltf-gui` bakes the VMD into a **standard glTF animation** (Bezier interpolation evaluated,
MMD's own deform order, IK already solved, 30 fps). UE's standard Interchange therefore imports it
as an `AnimSequence` named `<MeshName>_Anim`, and this plugin contains no VMD reader at all.

Step 3 looks for an `AnimSequence` that plays on the mesh's skeleton and assigns it to the Mesh
component (looping). To use a different motion, change `Anim to Play` on the Mesh component. When
several candidates share the skeleton, one is picked — preferring the same folder and a name that
starts with the mesh name — and the choice is written to the output log.

It coexists with the physics: the Post-Process Anim Blueprint runs afterwards regardless of the
playback mode, and bone-follow bodies are never written back, so the body follows the motion while
only hair and skirt get physics.

**Facial morphs are wired up by step 3 too** — but that part is a workaround for UE. UE 5.5's
Interchange drops glTF morph (`weights`) animation, so the `AnimSequence` ends up bone-only even
though the `.glb` carries the keys. Step 3 therefore reads those keys straight out of the `.glb` and
adds the curves whose names match the mesh's morph targets. It uses the `.glb` you picked in the
importer window, so **select both the mesh and the `.glb`** before running it. Curves that already
exist are left alone, so rebuilding never duplicates them. Step 3 also registers those curves on the
skeleton as morph-driving curves and **saves the skeleton asset** — UE 5.5 decides what drives a
morph target from that registration alone, so if it is not saved the face stops moving the next time
you open the editor even though the curves are still there.

Outline thickness is the component's `Outline Width Scale` (default 0.15); changing it takes effect
immediately. Materials whose PMX `flags` bit4 is not set get no outline, same as MMD. To add one to
an existing actor by hand, pick `Mmd Outline Component` from "+ Add" in the details panel — the mesh,
the follow behaviour and the per-material outline colors are all set up on the spot.

On startup the plugin cross-checks `extras.mmd` bone positions against the skeleton's reference pose
and logs an actionable error if the import convention does not match.

### Troubleshooting

| Symptom | What to check |
|---|---|
| Hair/skirt motion looks coarse or judders | On the MMD Physics node, **`FixedTimeStep` should be `1/60` (0.01667)** and **`SubSteps` 2**. At `1/30` the number of internal steps per frame alternates 0,1,0,1,1,... so the real-time update interval is uneven. To step finer, raise `SubSteps`, not `FixedTimeStep` (see [docs/porting_notes.md](docs/porting_notes.md)) |
| Motion is ~3x faster than MMD | `Gravity` must be 98 (PMX units). It is unrelated to UE world gravity |
| A semi-transparent material has hard, jagged edges | Check that its material instance's parent is `M_MmdToonTranslucent`. If it is not, the `.glb` material's `alphaMode` is not `BLEND` and `extras.mmd`'s `alphaClass` is not `"blend"` either. Materials whose `alphaClass` is `"mask"` are deliberately left on `M_MmdToon` (see "Known limitations" below) |
| Eyebrows/eyelashes do not show through the hair | Does that material have an `origTexture` (the un-prebaked texture)? If `無加工テクスチャ N` in the output log is 0, the `.glb` is an older export without `origTexture` — update the exporter (mmd2gltf-gui) and export again |
| A translucent edge is hard, or gets clipped at the threshold | Materials on the `M_MmdToon` (Masked) side are cut with the `.glb`'s `alphaCutoff`. Lowering `AlphaCutoff` on the material instance keeps more of the edge (the threshold lives in that parameter, not in the material's `OpacityMaskClipValue`, to avoid a static permutation per value) |
| Hair is too see-through / not see-through enough | `AlphaCutoff` (default 0.5) on the body hair material is the range drawn as an opaque core. The lower you set it, the more opaque it gets (same default as the source lilToon's `_SubpassCutoff`) |
| Overlapping hair strands saturate | Check that the `SoftPass` component is present — without it the second pass is never drawn |
| Translucent materials are drawn in the wrong order | Order follows the material slot order. Fix the material order in the model, or set `TranslucencySortPriority` on the skeletal mesh component in the level (that only orders the whole model against other actors) |
| The motion does not play | Are you placing the `BP_<MeshName>` from step 3? A bare skeletal mesh gets no animation assigned. If the output log says `アニメーション なし`, either the `.glb` has no motion baked in (converted without the exporter's `--vmd`), or no `AnimSequence` is bound to that skeleton |
| The face (expressions) does not move | Did you run step 3 **with the `.glb` also selected**? Morph curves are read straight from the `.glb`, because UE's import drops them. `表情モーフ 0 本` with a non-zero `既存のまま N` still means the curves are there; in that case check `スケルトンへ登録 N` in the same log (UE only drives morphs from curves registered on the skeleton — step 3 registers them and saves the skeleton). `表情モーフ 0 本` with `既存のまま 0` means the motion has no morph keys, or the morph names do not match the mesh's morph targets |
| No outlines appear | Are you placing the `BP_<MeshName>` from step 3? A bare skeletal mesh has no outline component. If it is there and still nothing shows, raise `Outline Width Scale`. Materials whose PMX `flags` bit4 is not set correctly get none |
| All outlines are the same color | Check that `EdgeColor` is set on the body material instances — the component reads the color from there |
| Outlines lag behind when the expression changes | Check that `MmdOutlineComponent` is updating every frame (`Draw Outline` enabled, component Tick not disabled) |
| Materials render grey | Look for `[MmdPhysics] マテリアルのコンパイルエラー` in the output log |
| Hair or skirt keeps swaying even when the model is standing still | Check that **`Use Split Impulse` and `Use Joint Split Impulse`** are both on in the MMD Physics node (they are on by default). With them off, positional correction leaks into real velocity and keeps pumping energy into the swaying bodies, which then resonate at their natural frequency. For the sway that remains even with them on, see "静止しているのに揺れ続ける" in [docs/porting_notes.md](docs/porting_notes.md) |
| Hair or skirt is stuck inside the body and never recovers | It has settled into a penetrating equilibrium, so waiting will not fix it. Call **`Reset MMD Physics`** from Blueprint (pass the Mesh component) to re-align the bodies to the current bone pose — do this after switching motions, on a loop wrap, or after teleporting the actor. Stopping and restarting PIE also resets it |
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
| UE 5.5 | Development and verification target. 20 automated tests green |
| UE 5.6 | Source-level compatibility only. **Not verified on a real install** |

## Known limitations

- **The model is assumed to be at the world origin.** Simulation runs in component space, so there is
  no momentum carry-over when the character moves. This limitation is inherited from the Unity version.
- **Outlines (edges) are drawn by one dedicated component, which is not added automatically.**
  Use step 3 ("Build Actor"), or add `MmdOutlineComponent` to the actor you placed in the level
  (see "Usage" above). Thickness is tunable in place via the component's `Outline Width Scale`.
- **Materials are Unlit.** This matches MMD, which exports with `KHR_materials_unlit`; shading comes
  from a light direction parameter (`LightDir` on `M_MmdToon` / `M_MmdToonTranslucent`) plus the toon
  ramp. The trade-off is that they do not respond to UE scene lights or Lumen.
- **Shared toons (`toon01`..`toon10`) are not bundled, but nothing breaks without them.**
  When they are missing the plugin **generates approximate toon ramps** in
  `{model folder}/SharedToon/T_MmdToonApprox01`..`10` and uses those, so one button press still
  gets you a shaded toon look. Those colors are our own approximation, not the originals.
  **Only if you want the exact original colors** do you need to import `toon01`..`toon10`
  yourself — put them anywhere, they are located by name from anywhere in `/Game` and take
  priority over the generated ramps. See [`Tools/make_toon_ramps.py`](Tools/make_toon_ramps.py)
  for how the approximate colors are tuned.
- **Ordering between translucent materials follows the material (slot) order.** UE's translucent sort
  key is `Priority → Distance → section order`, and sections of one skeletal mesh tie on the first two,
  so MMD's material order is reproduced as-is. However **`TranslucencySortPriority` is a
  `UPrimitiveComponent` property and cannot be set per slot**, so the Unity version's per-material
  `renderQueue = 3000 + slotIdx` nudging has no equivalent here.
- **Self-overlap inside a translucent material is solved with two components, and only for hair.**
  A UE material cannot hold two passes in one asset, so the work is split between the body
  (Masked — the core, which also writes depth) and `MmdSoftPassComponent`
  (Translucent — the soft tips). Something like eyebrows, a single sheet stuck onto the face, never
  overlaps itself, so it is blended with plain alpha instead — the same as MMD.
- **The look is tunable through material instance parameters.** `SubpassCutoff` (the threshold of the
  translucent opaque subpass) and `AlphaCutoff` (the mask threshold) are exposed, so you can dial them
  in from the editor without re-importing ([docs/porting_notes.md](docs/porting_notes.md)).
- **Opaque materials such as skin are not promoted to translucent, even when they have an
  `origTexture`.** The Unity version promoted every material carrying an `origTexture` and let
  lilToon's TwoPass write the depth. UE translucency cannot write depth, so making skin translucent
  lets later translucent materials (back hair, for instance) punch through the face. Instead the
  plugin **measures the alpha distribution over the UV region each material actually uses** and
  promotes only the semi-transparent decals stuck onto skin — eyebrows, eyelashes, forehead shadow —
  which is safe because the opaque skin underneath writes the depth. The measured values behind that
  classifier are in [docs/porting_notes.md](docs/porting_notes.md).
- **Morph (facial expression) animation is re-read from the `.glb` by the plugin.** UE 5.5's
  Interchange builds the track for a glTF `weights` channel **using the mesh node as its skeleton**
  (`ProcessMorphTargetAnimations` in `InterchangeGltfAnimation.cpp`), so it never merges with the
  bone track and is discarded — re-importing with stock settings reproduces it, giving an
  `AnimSequence` with 54 bone tracks and 0 curves. Step 3 therefore reads those keys out of the
  `.glb` itself and adds the curves, which is why **step 3 needs the `.glb`**. If UE ever fixes
  this, existing curves are left untouched, so the workaround simply becomes a no-op.
  Adding the curves is not enough on its own: `FBoneContainer` builds the
  `ECurveElementFlags::MorphTarget` flag purely from the skeleton's curve metadata, and
  `FAnimInstanceProxy` only feeds flagged curves to morph targets. Step 3 therefore registers the
  metadata (for curves that already exist too) and saves the skeleton asset.
  Separately, UV morphs (PMX morph type 3; 8 of them on IA) have no equivalent in UE morph targets,
  so they are neither imported nor given a curve.
- `sphereMode: 3` (sub-texture), `ambient` and `specular` are not supported
  (the Unity version does not support ambient/specular either).
- **Hitbox generation (the Unity version's step 3) is out of scope.**
- `SoftBody` (PMX 2.1) is not ported — the source builder does not use it either.
- The `.pmx` direct-read verification path (`PmxReader`) is not ported.
- Small jitter at rest is inherited from the source implementation.
- **Over long playback, hair and belt chains stretch slowly.** Measured: after 60 seconds
  a hair bone (右髪２) reaches 2.2–2.9x its bind length and a belt bone (腰ベルト２) 1.5–1.9x
  (3.3x at 120 seconds). The violent failure on
  long tail chains is fixed (see the table under "The physics engine is bit-exact with the source
  implementation"), but this slow drift remains. Call **`Reset MMD Physics`** from a Blueprint to
  snap everything back. The automated test `MmdPhysics.Editor.ChainStability` detects it.

## License

MIT License. See [LICENSE](LICENSE).

The bundled physics engine is a C++ port of
[`mmd2gltf-cs-physics`](https://github.com/masaka1024/mmd2gltf-cs-physics) (a C# reimplementation of
PMX 2.1 physics matched to Bullet 2.75 behaviour; MIT). Numerical parity is measured against that
same C# code as vendored in
[`mmd2gltf-unity-physics-importer`](https://github.com/masaka1024/mmd2gltf-unity-physics-importer)
(MIT), which is **linked, not copied**, into the build (see [INSTALL.md](INSTALL.md)).

No model data, motions, or MMD's bundled shared toon textures (`toon01`–`toon10`) are included.
