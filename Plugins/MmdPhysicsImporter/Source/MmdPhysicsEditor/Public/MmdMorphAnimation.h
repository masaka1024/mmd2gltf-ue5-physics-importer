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
//
// ★カーブを足すだけでは足りない。**スケルトン側の curve metadata**
//   (`FCurveMetaData::Type.bMorphtarget`) が立っていないと、UE はそのカーブを
//   モーフに繋がない。UE 5.5 は `FBoneContainer` を組むときに
//   スケルトン (または SkeletalMesh の UAnimCurveMetaData) のメタデータだけを見て
//   `ECurveElementFlags::MorphTarget` を決め、`FAnimInstanceProxy` は
//   そのフラグが立ったカーブしか `MorphTargetCurve` へ入れない
//   (BoneContainer.cpp の CacheRequiredAnimCurves / AnimInstanceProxy.cpp の
//   UpdateCurvesToEvaluationContext)。
//   したがって登録は**カーブを足したときだけでなく、既にカーブがある場合も**必要で、
//   さらに**スケルトンのアセットを保存**しないとエディタを開き直した時点で消える
//   (「体は踊るのに顔だけ動かない」という壊れ方をする)。
//
// ★記号だけのモーフ名 (`▲` `∧` `□` など) はカーブにできない。飛ばす。
//
//   UE 5.5 の AnimSequence は中身が **Sequencer のデータモデル**
//   (AnimationData プラグイン / 既定で有効) で、float カーブの実体は
//   FK ControlRig のスカラーパラメータチャンネルとして持たれている。
//   そのチャンネルの引き当ては
//     URigHierarchy::GetSanitizedName(カーブ名)
//   で行われ (AnimSequencerController.cpp)、サニタイズは
//   **英数字と `_ - . |` 以外を `_` に潰す** (RigHierarchy.cpp の SanitizeName)。
//   仮名・漢字は FChar::IsAlpha を通るので無事だが、記号は潰れる。
//
//   これが二重に効く:
//     1. 潰れた名前が衝突する。`▲` も `∧` も `□` も `_` になるので、
//        2 本目のカーブを足した時点で 1 本目と同じチャンネルを掴み、
//        UE 5.5 は `check(bUpdateKey || ...)` で**エディタごと落ちる**
//        (AnimSequencerController.cpp:3386)。
//     2. 衝突しなくても動かない。カーブを足すと UE は
//        RegenerateLegacyCurveData() でカーブ一覧を**リグ側の名前から作り直す**ため、
//        `はんっ！` のカーブは `はんっ_` に化けてモーフ名と一致しなくなる。
//
//   どのみち動かせないので、名前がサニタイズで変わるトラックは足さずに数えて報告する。
//   (UE 側がサニタイズ前の名前で引き当てるようになったら、この除外は消せる)
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
	/**
	 * リグの名前規則に載らない名前だったので飛ばした数 (下の注記を参照)。
	 * `▲` `∧` `□` のような記号だけの MMD 標準モーフがここに入る。
	 */
	int32 SkippedUnsafeName = 0;
	/** 上で飛ばしたモーフ名。ログとメッセージに出す。 */
	TArray<FString> UnsafeNames;
	/**
	 * スケルトンへ「モーフを動かすカーブ」として登録し直した数。
	 * ★0 でないなら **スケルトンのアセットを保存しないと次のセッションで効かない**。
	 *   呼び出し側 (FMmdActorBuilder) が保存する。
	 */
	int32 MorphMetaDataSet = 0;
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

	/**
	 * UE のリグ階層が名前に許す文字だけにした名前を返す。
	 * 返り値が引数と違うモーフはカーブにできない (上の注記を参照)。
	 *
	 * ★ControlRig の URigHierarchy::SanitizeName と同じ規則を写したもの。
	 *   これ 1 箇所のために ControlRig モジュールへ依存したくないので手元に持つ。
	 */
	static FString MakeRigSafeName(const FString& In);
};
