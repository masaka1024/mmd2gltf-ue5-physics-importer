// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【3】アクターを生成 — 本体と輪郭線を組んだ Blueprint を作る。
//
// 【1】と【2】が作るのはアセット (メッシュ・AnimBP・マテリアル) だけで、
// 輪郭線はコンポーネントなのでアクター側にしか置けない。毎回手で足すのは面倒なので、
// 組み上がった Blueprint アクターを 1 つ吐く。
//
//   BP_<メッシュ名>
//   └─ SkeletalMeshComponent (ルート)   … メッシュを割り当て。物理は Post-Process AnimBP で効く
//      └─ MmdOutlineComponent           … 輪郭線 (反転ハル)
//
// 物理は【1】がスケルタルメッシュ側に Post-Process AnimBP を割り当てているので、
// メッシュを指定するだけで効く。ここでは配線しない。
//
// ★UI から独立した関数にしてあるのは、ヘッドレスの自動テストから直接呼べるようにするため。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UBlueprint;

struct MMDPHYSICSEDITOR_API FMmdActorResult
{
	bool bSuccess = false;
	UBlueprint* Blueprint = nullptr;
	/** 輪郭線コンポーネントを付けたか (輪郭線を描く材質が無ければ付けない)。 */
	bool bHasOutline = false;
	/** 2 パス目 (柔らかい毛先) のコンポーネントを付けたか。 */
	bool bHasSoftPass = false;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdActorBuilder
{
public:
	/**
	 * 本体 + 輪郭線を組んだ Blueprint アクターを作る (既にあれば作り直す)。
	 *
	 * @param Mesh 対象スケルタルメッシュ。生成先はこのアセットと同じフォルダ
	 */
	static FMmdActorResult BuildActor(USkeletalMesh* Mesh);
};
