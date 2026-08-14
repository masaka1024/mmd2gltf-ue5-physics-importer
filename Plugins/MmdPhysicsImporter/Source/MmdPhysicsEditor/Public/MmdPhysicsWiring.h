// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【1】物理を配線 — Unity 版 MmdPhysicsImporterWindow.AttachCustomEngine() に対応。
//
// スケルタルメッシュに Post-Process Anim Blueprint を作って割り当て、その AnimGraph に
// MMD Physics ノードを 1 つ置いて配線する。
//
//   Input Pose ─▶ (自動変換) ─▶ MMD Physics ─▶ (自動変換) ─▶ Output Pose
//
// Post-Process AnimBP はメインの AnimGraph 評価の後に走るので、移植元 Unity 版が
// LateUpdate で書き戻していたのと同じ位置になる。体のボーン (BoneFollow) には
// 書き戻さないので、ダンスの動きはそのまま残る。
//
// ★UI から独立した関数にしてあるのは、ヘッドレスの自動テストから直接呼べるようにするため。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimBlueprint;

struct MMDPHYSICSEDITOR_API FMmdWireResult
{
	bool bSuccess = false;
	UAnimBlueprint* AnimBlueprint = nullptr;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdPhysicsWiring
{
public:
	/**
	 * 物理を配線 / 再配線する。
	 *
	 * @param Mesh       対象スケルタルメッシュ (UE 標準の Interchange glTF で取り込んだもの)
	 * @param GlbPath    mmd2gltf-gui が出力した .glb の絶対パス
	 * @param UnitScale  PMX 1 単位あたりのメートル (0 以下なら .glb の extras.mmd に従う)
	 */
	static FMmdWireResult WirePhysics(USkeletalMesh* Mesh, const FString& GlbPath, float UnitScale = 0.0f);

	/** 割り当て済みの Post-Process Anim Blueprint を外す。 */
	static FMmdWireResult UnwirePhysics(USkeletalMesh* Mesh);
};
