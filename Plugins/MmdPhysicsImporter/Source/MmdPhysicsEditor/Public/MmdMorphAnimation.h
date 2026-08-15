// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 表情モーフのアニメーションを AnimSequence へ足す。
//
// ★これは **UE 側の取りこぼしを埋めるための処理**であって、仕様の実装ではない。
//
//   mmd2gltf-gui は VMD のモーフキーを glTF 標準の `weights` アニメーションとして
//   焼き込んでおり、データは仕様どおり `.glb` に入っている (IA の実測で
//   1699 キー × 45 トラック)。ところが UE 5.5 の Interchange は `weights` チャンネルの
//   トラックノードを **メッシュノードをスケルトンとして** 作るため
//   (InterchangeGltfAnimation.cpp の ProcessMorphTargetAnimations)、
//   ボーン用のトラックノード (root joint の Uid) と結合されず、そのまま落ちる。
//   素の設定で取り込み直しても再現し、出来上がる AnimSequence は
//   ボーン 54 トラック・カーブ 0 本になる。
//
//   `weights` チャンネルの target は glTF 仕様上メッシュを持つノードしか指せないので、
//   エクスポーター側で回避する余地は無い。そこで取り込みのあとに、
//   `.glb` から直接キーを読んで AnimSequence へカーブとして足す。
//
// ★UE 側が直ったら丸ごと消せるように、1 ファイルに閉じてある。
//   判定は「そのカーブが既にあるか」だけなので、直った UE では
//   既存カーブを上書きせず何もしない (下の bSkipExisting を参照)。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimSequence;

struct MMDPHYSICSEDITOR_API FMmdMorphAnimResult
{
	bool bSuccess = false;
	/** .glb 側のモーフトラック数 (= mesh.extras.targetNames の数)。 */
	int32 TotalTracks = 0;
	/** 実際に足したカーブの数。 */
	int32 CurvesAdded = 0;
	/** 足したキーの総数。 */
	int32 KeysAdded = 0;
	/**
	 * 対応するモーフターゲットがメッシュに無くて飛ばした数。
	 * UV モーフ (PMX のモーフ種別 3) は UE のモーフターゲットに対応概念が無く、
	 * Interchange も取り込まないのでここに入る。異常ではない。
	 */
	int32 SkippedNoMorphTarget = 0;
	/** 既にカーブがあったため触らなかった数 (UE 側が直った場合など)。 */
	int32 SkippedExisting = 0;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdMorphAnimation
{
public:
	/**
	 * `.glb` の `weights` アニメーションを読んで、AnimSequence へモーフカーブを足す。
	 *
	 * @param Mesh     モーフターゲットの名前を突き合わせる相手
	 * @param Anim     カーブを足す先 (Interchange が取り込んだもの)
	 * @param GlbPath  mmd2gltf-gui が出力した .glb
	 */
	static FMmdMorphAnimResult ApplyMorphCurves(USkeletalMesh* Mesh, UAnimSequence* Anim, const FString& GlbPath);
};
