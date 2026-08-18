// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 古い .glb 向けの保険 — 移動できないボーンの translation を参照ポーズへ戻す。
//
// ★これは仕様の実装ではなく、**修正前のエクスポーターが出した .glb を
//   読まされたときにだけ効く後始末**である。
//
//   MMD は VMD の移動値を「移動可 (PMX ボーンフラグ 0x0004)」のボーンにしか
//   適用しない。ところが物理焼き込み系ツールが書き出したフルキー VMD は
//   回転専用のボーンにも移動値を残しており、mmd2gltf-gui は `aa5cc7b`
//   (2026-08-17) まで、それをそのまま適用して glTF の translation チャンネルとして
//   出していた。受け手はそれを「そのボーンはそこへ動く」と読むほかないので、
//   鎖のボーン間距離が参照ポーズから外れる (UE5 では鎖が床まで伸びる)。
//
//   エクスポーターは直っているので、出し直した .glb ではここは何もしない。
//   **出し直せない手持ちの .glb のために**、取り込みのあとで落とす。
//
// ★消して良い条件: 手元の .glb がすべて `aa5cc7b` 以降の出力になったら、
//   このファイルごと消せる。呼び出しは FMmdActorBuilder::BuildActor の 1 箇所だけ。
//
// ★判定 (エクスポーター側 extrasMmd_schema.md の bones の項):
//     translation チャンネルを持って良いのは
//       - 移動可ボーン (flags & 0x0004)          … 常に正当。触らない
//       - 物理ベイクが位置を焼くボーン (mode 1/2) … **条件付き**で正当 (下記)
//     の 2 つだけ。それ以外のボーンの translation は参照ポーズの値で埋め直す。
//
// ★mode 1/2 のボーンも、壊れていれば戻す。
//   実測 (Tda式初音ミク・アペンド Ver1.10 / 修正前の .glb): 物理を切って
//   アニメーションだけ流しても **しっぽ１３ が 22.96 倍 / しっぽ１２ が 0.53 倍**。
//   この 2 本はどちらも mode 2 / mode 1 の剛体付きで、「物理ベイクだから正当」と
//   して見逃すと保険が何もしないことになる。原因はエクスポーターが鎖の親を
//   取り違えて焼いていたこと (吊りジョイントが末端を盗む。mmd2gltf-gui の
//   `aa5cc7b` で修正済み) で、**フラグからは正当な焼きと区別できない**。
//
//   区別できるのは長さ。エクスポーターの仕様は物理ベイクの並進について
//   「自分のバインド基準で比 1.0 近傍」を保証しており (MmdChainStabilityTest.cpp
//   の注記と同じ根拠)、親との距離が参照ポーズの 2 倍 / 1/2 倍を外れることは無い。
//   そこを外れたキーが 1 つでもあるトラックは焼き損じとみなして戻す。
//   ★親との距離 = ローカル並進の長さ。ボーンのローカル並進は「親から見た自分の
//     位置」なので、そのまま鎖の 1 節の長さになる (ChainStability が測る量と同じ)。
//
//   ★「怪しいトラックを消す」のではなく**参照ポーズで埋める**こと。
//     トラックごと消すと UE は回転まで巻き戻してしまう。ここで直したいのは
//     並進だけで、同じトラックに乗っている回転は正しいデータである。
//
// ★フラグの読み場所は 2 つある。新しい .glb は `extras.mmd.bones[i].flags` を
//   持つが、古い .glb には無い。ただし**古い .glb にも
//   `nodes[i].extras.mmd.flags` は入っている** (2026-08-12 版の IA で確認:
//   179 ノードすべてにある) ので、保険が実際に効くのはこちらの経路。
//   どちらも無い .glb では判定材料が無いので**何もしない** (触らないのが正しい)。
//
// ★ボーン番号は `skins[0].joints` の並びで決まる (GlbPhysicsReader と同じ規則)。
//   剛体の `bone` はこの番号を指すので、名前へ引き当てるのにこの順が要る。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"
#include "MmdMiniJson.h"

class UAnimSequence;

/** .glb から読んだボーン 1 本ぶん (名前と PMX の生フラグ)。並びは PMX ボーン順。 */
struct MMDPHYSICSEDITOR_API FMmdGlbBone
{
	FString Name;
	int32 Flags = 0;
};

struct MMDPHYSICSEDITOR_API FMmdTranslationFixResult
{
	bool bSuccess = false;
	/** .glb から読めたボーン数 (0 ならフラグが無く、判定していない)。 */
	int32 BonesRead = 0;
	/** フラグを extras.mmd.bones から読めたか (= `aa5cc7b` 以降の新しい出力)。 */
	bool bFromBonesArray = false;
	/** .glb 側にボーンがあり、判定できたトラック数。 */
	int32 TracksChecked = 0;
	/** 参照ポーズへ戻したトラック数。 */
	int32 TracksReset = 0;
	/** 書き換えたキーの総数。 */
	int32 KeysReset = 0;
	/** 上のうち、物理ベイク (mode 1/2) のボーンだったもの = 焼き損じ。 */
	int32 TracksResetBaked = 0;
	/** .glb 側に同名のボーンが無く、判定しなかったトラック数 (仮想ボーン等)。 */
	int32 TracksUnknown = 0;
	/** 戻したトラックの中で、参照ポーズから最も離れていた距離 [cm]。 */
	double WorstOffsetCm = 0.0;
	/** 焼き損じとして戻した中で、参照ポーズ長から最も外れていた比 (1.0 = 一致)。 */
	double WorstLenRatio = 1.0;
	/** 戻したボーン名。ログに出す。 */
	TArray<FString> ResetBones;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdBoneTranslationFix
{
public:
	/** PMX ボーンフラグ「移動可」。 */
	static constexpr int32 KBoneFlagMovable = 0x0004;

	/**
	 * 参照ポーズからこれ以下のずれは触らない [cm]。
	 * 取り込みの丸め誤差でトラック全体を書き換えないためのしきい値。
	 */
	static constexpr double KOffsetEpsilonCm = 0.01;

	/**
	 * 物理ベイク (mode 1/2) の並進を「焼き損じ」と判断する、参照ポーズ長に対する比。
	 * ★MmdChainStabilityTest の合否と同じ値にしてある。
	 *   あちらが「破綻」と呼ぶものをこちらが直す、という対応にするため。
	 */
	static constexpr double KBakedLenRatioMax = 2.0;
	static constexpr double KBakedLenRatioMin = 0.5;

	/**
	 * 取り込み済み AnimSequence を検査し、translation を持ってはいけないボーンの
	 * 並進キーを参照ポーズの値で埋め直す。
	 *
	 * @param Anim     Interchange が取り込んだ AnimSequence (中身を書き換える)
	 * @param GlbPath  mmd2gltf-gui が出力した .glb (ボーンフラグと剛体の取得元)
	 *
	 * ★呼び出し側は TracksReset > 0 のときアセットを保存すること。
	 */
	static FMmdTranslationFixResult Apply(UAnimSequence* Anim, const FString& GlbPath);

	/**
	 * .glb の JSON からボーン (名前 + PMX フラグ) を PMX ボーン順に集める。
	 * extras.mmd.bones を優先し、無ければ skins[0].joints → nodes[].extras.mmd.flags。
	 * フラグがどこにも無ければ false (判定材料が無い .glb)。
	 */
	static bool ReadBones(const MmdPhysics::FMmdJsonObject* Root, TArray<FMmdGlbBone>& OutBones,
		bool& bOutFromBonesArray);

	/**
	 * translation の扱いが変わる 2 つの集合に分ける (上の判定を参照)。
	 *
	 * @param OutMovable       移動可ボーン。並進は常に正当なので触らない
	 * @param OutPhysicsBaked  mode 1/2 剛体が付いたボーン。長さが保たれている間だけ正当
	 *
	 * どちらにも入らないボーンの並進は、ずれていれば無条件で戻す。
	 */
	static void CollectBoneCategories(const MmdPhysics::FMmdJsonObject* Root,
		const TArray<FMmdGlbBone>& Bones, TSet<FString>& OutMovable, TSet<FString>& OutPhysicsBaked);
};
