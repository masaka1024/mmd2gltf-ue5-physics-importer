// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【3】アクターを生成 — 本体と輪郭線を組んだ Blueprint を作る。
//
// 【1】と【2】が作るのはアセット (メッシュ・AnimBP・マテリアル) だけで、
// 輪郭線はコンポーネントなのでアクター側にしか置けない。毎回手で足すのは面倒なので、
// 組み上がった Blueprint アクターを 1 つ吐く。
//
//   BP_<メッシュ名>
//   └─ SkeletalMeshComponent (ルート)   … メッシュとアニメーションを割り当て。物理は Post-Process AnimBP で効く
//      └─ MmdOutlineComponent           … 輪郭線 (反転ハル)
//
// 物理は【1】がスケルタルメッシュ側に Post-Process AnimBP を割り当てているので、
// メッシュを指定するだけで効く。ここでは配線しない。
//
// ★アニメーション (VMD 由来のモーション) はここで繋ぐ。
//   mmd2gltf-gui は VMD を **glTF 標準のアニメーション**として焼き込むので、
//   UE 標準の Interchange が AnimSequence として取り込んでいる。あとは
//   コンポーネントに割り当てるだけなのに、それをしないと置いても動かない。
//   Post-Process AnimBP は再生モードに関わらず後段で必ず走るため、
//   単発再生 (AnimationSingleNode) にしても物理とは共存する。
//
// ★UI から独立した関数にしてあるのは、ヘッドレスの自動テストから直接呼べるようにするため。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UBlueprint;
class UAnimSequence;

struct MMDPHYSICSEDITOR_API FMmdActorResult
{
	bool bSuccess = false;
	UBlueprint* Blueprint = nullptr;
	/** 輪郭線コンポーネントを付けたか (輪郭線を描く材質が無ければ付けない)。 */
	bool bHasOutline = false;
	/** 2 パス目 (柔らかい毛先) のコンポーネントを付けたか。 */
	bool bHasSoftPass = false;
	/** 本体コンポーネントに割り当てたアニメーション (見つからなければ nullptr)。 */
	UAnimSequence* Animation = nullptr;
	/** 同じスケルトンで再生できた AnimSequence の数 (2 以上なら 1 本を選んでいる)。 */
	int32 AnimationCandidates = 0;
	/**
	 * アニメーションへ足した表情モーフのカーブ数 (.glb を渡したときだけ)。
	 * UE 5.5 の Interchange は glTF のモーフアニメーションを取りこぼすため、
	 * ここで .glb から直接読んで補っている (MmdMorphAnimation.h の注記を参照)。
	 */
	int32 MorphCurvesAdded = 0;
	/**
	 * 取り込みの時点で名前が化けていて消したカーブ数 (.glb を渡したときだけ)。
	 * `▲` `∧` `□` のような記号モーフは UE のリグ規則に載らず、Interchange が
	 * カーブを作れた場合も `_` のような名前へ潰れてどのモーフにも繋がらない。
	 * 放っておくと残り続けるので消している (MmdMorphAnimation.h の注記を参照)。
	 */
	int32 MorphCurvesRemoved = 0;
	/**
	 * 参照ポーズへ戻した translation トラック数 (.glb を渡したときだけ)。
	 * 0 でないなら **その .glb が古い**。修正前のエクスポーターが移動できないボーンへ
	 * 移動値を焼いていたぶんで、本来は出し直すのが正しい
	 * (MmdBoneTranslationFix.h の注記を参照)。
	 */
	int32 TranslationTracksReset = 0;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdActorBuilder
{
public:
	/**
	 * 本体 + アニメーション + 輪郭線を組んだ Blueprint アクターを作る (既にあれば作り直す)。
	 *
	 * @param Mesh    対象スケルタルメッシュ。生成先はこのアセットと同じフォルダ
	 * @param GlbPath mmd2gltf-gui が出力した .glb。渡すと表情モーフのアニメーションも補う
	 *                (省略するとボーンだけ。UE の取りこぼしの穴埋めなので .glb が要る)
	 */
	static FMmdActorResult BuildActor(USkeletalMesh* Mesh, const FString& GlbPath = FString());

	/**
	 * メッシュのスケルトンで再生できる AnimSequence を探す。
	 *
	 * 同じフォルダにあるもの → 名前がメッシュ名で始まるもの、の順に優先する
	 * (Interchange は `<メッシュ名>_Anim` という名前でメッシュの隣に置く)。
	 *
	 * @param OutCandidates 条件に合った AnimSequence の総数 (選ばなかったものも含む)
	 */
	static UAnimSequence* FindAnimationFor(USkeletalMesh* Mesh, int32& OutCandidates);
};
