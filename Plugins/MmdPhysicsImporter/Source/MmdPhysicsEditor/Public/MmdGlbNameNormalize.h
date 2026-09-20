// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 取り込み用に .glb のボーン名・モーフ名の全角数字を半角へ直した複製を作る。
//
// ★これは **UE 側の取りこぼしを埋めるための処理**であって、仕様の実装ではない。
//   全角数字が URigHierarchy::SanitizeName を通せない理由は MmdNameNormalize.h を参照。
//
// ★原本 .glb は書き換えない。エクスポーター (mmd2gltf-gui) も直さない。
//   エクスポーターの目的は原典 DATA を変えずに保持することなので、
//   半角化はエクスポーターではなく**取り込み側の内部**でやる。
//   複製は Saved/MmdPhysicsImporter/ に置く (破棄してよい中間物)。
//
// ★「Interchange のパイプラインでボーン名を直す」案とは別物。あちらは成立しない。
//   スケルトンのボーン名とトラック名はジョイントノードの GetDisplayLabel() から
//   取られるので揃うが、**スキンウェイトだけが取り残される**。頂点ウェイトの
//   ボーン番号はメッシュのペイロードが持つ JointNames で張り替えられ、そちらは
//   翻訳器が .glb から直接読んだ名前 (= 全角のまま) だから。
//
//     // InterchangeSkeletalMeshFactory.cpp:600 付近
//     SourceRemapBoneIndex[LocalJointIndex] = LocalJointIndex;   // 既定は素通し
//     if (Bone.Name.Equals(LocalJointName))                      // 半角 vs 全角 → 一致しない
//     {
//         SourceRemapBoneIndex[LocalJointIndex] = RefBoneIndex;  // ここに到達しない
//     }
//
//   一致しないと張り替えが起きず、頂点が別のボーンに付いてメッシュが崩れる。
//   **ファイルの中身そのものを差し替えれば**、翻訳器もファクトリも同じ半角名を見るので
//   この不整合が原理的に消える。それがこのファイルの存在理由。
//
// ★剛体の紐づけは壊れない。extras.mmd の剛体はボーンを**インデックス**で持つ
//   (GlbPhysicsReader.cpp の `Rb.BoneIndex = Int(Get(r, "bone"))`)。
//   名前は nodes[].name から引いているだけなので、名前を直しても対応は動かない。
//
// ★JSON チャンク以外は 1 バイトも触らない。BIN チャンク (メッシュ・テクスチャ) は
//   そのまま複写する。JSON も、書き換える名前のトークン以外は**原文のまま**複写して
//   再エンコードしない (浮動小数の丸めで値が変わるのを避けるため。
//   GlbPhysicsReader::ExtractJsonChunkGlb と同じ方針)。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

/** 半角化の結果。失敗しても何が起きたかを UI へ出せるように、数と理由を持たせる。 */
struct MMDPHYSICSEDITOR_API FMmdGlbNormalizeResult
{
	bool bSuccess = false;

	/** 書き換えたボーン名の数。0 なら元から全角数字が無かった (複製する必要も無い)。 */
	int32 BonesRenamed = 0;

	/** 書き換えたモーフ名の数。 */
	int32 MorphsRenamed = 0;

	/** 原本名 -> UE 名 (変わるものだけ)。原本の名前を失わないよう呼び出し側へ返す。 */
	TMap<FString, FString> NameMap;

	/**
	 * 半角化すると名前がぶつかったグループ。中止はせず、一意な名前へ振り分けて取り込む
	 * (規則は NameNormalize::BuildUeNameMap)。警告ログにも同じ内容を出す。
	 */
	TArray<FString> Collisions;

	TArray<FString> Warnings;
	FString Message;

	/** 書き換えが 1 件も無い = 原本をそのまま取り込んでよい。 */
	bool IsNoOp() const { return BonesRenamed == 0 && MorphsRenamed == 0; }
};

class MMDPHYSICSEDITOR_API FMmdGlbNameNormalize
{
public:
	/**
	 * GLB のバイト列を半角化する。
	 *
	 * 書き換える対象は 2 箇所だけ:
	 *   - skins[].joints が指すノードの nodes[].name  (= スケルトンのボーン名)
	 *   - meshes[].extras.targetNames と meshes[].primitives[].extras.targetNames
	 *     (= モーフターゲット名)
	 *
	 * ★マテリアル名やテクスチャ名は対象にしない。全角数字を含んでいても
	 *   サニタイズの経路に乗らないので直す理由が無く、直すとアセット名が変わる。
	 *
	 * @param OutGlb  半角化した GLB。書き換えが 0 件のときは InGlb の複製をそのまま入れる。
	 */
	static FMmdGlbNormalizeResult NormalizeBytes(const TArray<uint8>& InGlb, TArray<uint8>& OutGlb);

	/** ファイル版。OutPath の親ディレクトリは必要なら作る。 */
	static FMmdGlbNormalizeResult NormalizeFile(const FString& InPath, const FString& OutPath);

	/** 取り込み用複製の置き場所: Saved/MmdPhysicsImporter/<元の名前>/<元の名前>.glb (ファイル名は原本と同じ) */
	static FString MakeNormalizedPath(const FString& SourceGlbPath);
};
