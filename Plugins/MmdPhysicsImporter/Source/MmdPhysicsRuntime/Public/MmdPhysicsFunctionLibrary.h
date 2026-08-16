// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Blueprint から MMD 物理を操作するための関数。
//
// 物理ノード (FAnimNode_MmdPhysics) は Post-Process Anim Blueprint の中にいるため、
// 通常の Blueprint からは直接触れない。ここでスケルタルメッシュコンポーネントを
// 起点にノードを探して叩く。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MmdPhysicsFunctionLibrary.generated.h"

class USkeletalMeshComponent;

UCLASS()
class MMDPHYSICSRUNTIME_API UMmdPhysicsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 剛体を今のボーン姿勢へ再整合し、速度と接触を捨てる (移植元 ResetPhysicsToBones 相当)。
	 *
	 * 髪やスカートが体に潜り込んだまま戻らなくなったときの復帰手段。
	 * 潜り込んだ状態は「貫入したまま釣り合っている」ので、放っておいても直らない。
	 *
	 * 使いどころ:
	 * - モーションを切り替えた直後 (姿勢が飛ぶため)
	 * - ループの折り返し (末尾から先頭へ瞬間移動するため)
	 * - アクターをテレポートさせた直後
	 *
	 * 再整合は次の評価から `Pose Reset Delay Frames` 回ぶん行う。アニメーションが
	 * 新しい姿勢を適用した後の骨格に合わせる必要があるため、1 フレームでは足りない。
	 *
	 * @param Component 対象のスケルタルメッシュコンポーネント (BP_<メッシュ名> の Mesh)
	 * @return 実際にリセットした物理ノードの数。0 なら物理が配線されていない
	 */
	UFUNCTION(BlueprintCallable, Category = "MMD Physics",
		meta = (DisplayName = "Reset MMD Physics", DefaultToSelf = "Component"))
	static int32 ResetMmdPhysics(USkeletalMeshComponent* Component);
};
