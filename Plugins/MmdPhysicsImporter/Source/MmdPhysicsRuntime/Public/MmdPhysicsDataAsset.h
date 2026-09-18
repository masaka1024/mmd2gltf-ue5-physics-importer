// Copyright (c) 2026 masaka1024. MIT License.
//
// パッケージしたビルドへ物理データを同梱するためのアセット。
//
// ★なぜ要るか
//   FAnimNode_MmdPhysics は当初 .glb の**絶対パス**を持ち、実行時に読んでいた。
//   これは開発機でしか動かない:
//     ・.glb は UAsset ではないので**クックされない** (パッケージに入らない)
//     ・"C:\Users\..." という絶対パスは配布先のマシンには存在しない
//   結果、パッケージしたビルドでは物理ワールドが構築されないまま**無言で終わる**。
//   移植元の Unity 版が APK で同じ壊れ方をして直した (74fcc16 / physics 本体 6665392)。
//
// ★中身は「JSON チャンクだけの最小 GLB」
//   物理に要る extras.mmd とボーン階層 (nodes/skins) はすべて JSON チャンク側にあり、
//   BIN チャンク (メッシュ/テクスチャ) は PmxPhysicsModel の構築に一切使わない。
//   落とすと元の数%まで縮む (移植元の実測で 0.7〜2.6%)。
//   バイト列は元の GLB からそのまま複写する (再エンコードしない) ので、
//   .glb を直読みしたときと結果はビット単位で同一になる。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MmdPhysicsDataAsset.generated.h"

/**
 * 揺れ物の物理データ (剛体・ジョイント・ボーン階層) を UAsset として持つ。
 * FMmdPhysicsWiring::WirePhysics が .glb から作り、AnimGraph のノードへ割り当てる。
 */
UCLASS(BlueprintType)
class MMDPHYSICSRUNTIME_API UMmdPhysicsData : public UObject
{
	GENERATED_BODY()

public:
	/** 作成元 .glb のファイル名 (診断用。パスは持たない — 配布先で意味を持たないため)。 */
	UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
	FString SourceGlbName;

	/** extras.mmd の unitScale。ノード側の設定と食い違っていないか確かめるために持つ。 */
	UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
	float UnitScale = 0.08f;

	/** 剛体数 / ジョイント数 (診断用。実データは Glb 側にある)。 */
	UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
	int32 NumRigidBodies = 0;

	UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
	int32 NumJoints = 0;

	/**
	 * JSON チャンクだけを残した GLB。GlbPhysicsReader::LoadBytes がそのまま読める
	 * (ParseGlb は BIN チャンクが無くても成立する)。
	 * ★エディタ表示には出さない。数百 KB の配列を詳細パネルへ出しても読めないため。
	 */
	UPROPERTY()
	TArray<uint8> Glb;

	bool HasData() const { return Glb.Num() > 0; }
};
