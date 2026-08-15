// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 【2】マテリアルを MMD トゥーンへ変換 — Unity 版の lilToon 変換に対応する。
//
// UE には lilToon 相当が無いため、マスターマテリアル M_MmdToon を
// **プログラムでノードグラフごと生成**し、マテリアルごとにインスタンスを作る。
// (バイナリの .uasset をリポジトリに置かずに済み、UE のバージョン差にも追随しやすい)
//
// シェーディングは Unlit + 自前の N·L トゥーン。MMD のマテリアルは
// KHR_materials_unlit 付きで出力されており、MMD 本体の陰影もライト方向と
// トゥーンランプだけで決まるため、この方が見た目が近い。
// 代わりにシーンのライトには反応しない (ライト方向はマテリアルのパラメータ)。
//
// アウトライン (エッジ) は今回は描画しない。edgeColor / edgeSize は
// マテリアルインスタンスのパラメータとして保存だけしておく。
//
// ★マスターは 2 種類ある (Masked / Translucent)。どちらへ繋ぐかは
//   glTF の alphaMode と extras.mmd の alphaClass / origTexture から決める。
//   判断は FMmdMaterialConversion::PlanMaterial に切り出してあるので、
//   分岐の理由はそちらの注記を参照。
//
// ★toon / sphere / origTexture のテクスチャは glTF の標準マテリアルから
//   参照されていないため、UE の Interchange ではアセット化されない。
//   名前で見つからないものは GLB バイナリから直接取り出してアセット化する
//   (MmdPhysics::GlbImageExtractor)。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"
#include "MmdGlbMaterialReader.h"

class USkeletalMesh;
class UMaterial;
class UTexture2D;

/**
 * マスターマテリアルの種類。glTF の alphaMode で使い分ける。
 *
 * UE のブレンドモードはマテリアル単位の静的スイッチなので、1 枚のマスターでは両立できない
 * (インスタンスの BasePropertyOverrides で上書きすると、インスタンスごとに静的permutationが
 *  増えるうえ、Masked 用のグラフには Opacity 入力が繋がっておらず不透明になる)。
 *
 * - Masked      : alphaMode = OPAQUE / MASK。BaseColorTex のアルファでしきい値カット。
 *                 不透明パスで描かれ深度を書く。MMD の「アルファテスト材質」に相当し、
 *                 移植元 Unity 版の AlphaTest 帯 (renderQueue 2452+) と同じ役割を持つ。
 * - Translucent : alphaMode = BLEND、または origTexture により昇格した真の半透明。
 *                 BaseColorTex のアルファでそのままブレンドする。
 *                 IA の眉のような半透明グラデーションが、しきい値で切り抜かれずに出る。
 */
enum class EMmdMasterVariant : uint8
{
	Masked,
	Translucent,
};

/**
 * 材質が実際に使っている UV 領域だけを見たアルファの分布。
 *
 * ★MMD のモデルは 1 枚のテクスチャを「不透明な肌」と「半透明の貼り付け
 *   (眉・まつげ・額の影・チーク)」で共有する。テクスチャ全体のヒストグラムでは
 *   どちらも同じに見えるので、材質ごとに UV 領域を絞って測らないと区別できない。
 *   実測 (IA): 肌 mat1/mat3 は半透明率 0〜1%、眉 mat4 は 43%、額の影 顔3 は 29%。
 *
 * エクスポーター側の `_material_uv_alpha_stats` と同じ考え方。
 */
struct MMDPHYSICSEDITOR_API FMmdUvAlphaStats
{
	/** 標本数 (材質の頂点数。少なすぎると判定しない)。 */
	int32 Samples = 0;
	/** 0.02 < α < 0.98 の割合。 */
	float FracSemi = 0.0f;
	/** α >= 0.98 の割合。 */
	float FracOpaque = 0.0f;
	/** α の平均。 */
	float MeanAlpha = 0.0f;

	bool IsValid() const { return Samples >= 3; }
};

/**
 * 1 材質をどう変換するかの判断結果。
 *
 * ★extras.mmd と glTF の値だけで決まる純粋な判断なので、アセットを触る処理から分けてある
 *   (実モデルが無い環境でも分岐を自動テストできるようにするため)。
 */
struct MMDPHYSICSEDITOR_API FMmdMaterialPlan
{
	/** Translucent マスターへ繋ぐか (= 真の半透明として、しきい値で切らずに混ぜる)。 */
	bool bTranslucent = false;

	/** extras.mmd の origTexture (プリベイク前の無加工版) を BaseColorTex に使うか。 */
	bool bUseOrigTexture = false;

	/**
	 * origTexture により Masked → Translucent へ昇格したか
	 * (alphaMode=BLEND で元から半透明のものは含まない)。ログと集計用。
	 */
	bool bPromotedByOrigTexture = false;

	/**
	 * 肌などに貼り付けられた半透明の材質 (眉・まつげ・額の影・チーク) と判定されたか。
	 * この組は昇格させても安全: 下地の肌が Masked で深度を書くため、
	 * 後ろ髪のような後段の半透明が顔を突き抜けることはない。
	 */
	bool bOverlay = false;

	/**
	 * 半透明のうち、α >= SubpassCutoff を不透明として描くか
	 * (移植元 lilToon の TwoPass = 不透明サブパス + ブレンドパス に相当)。
	 *
	 * 移植元は「半透明かつテクスチャを持つ材質」にだけ TwoPass を使い、
	 * テクスチャの無い単色ガラス (レンズ等) は素のブレンドのままにしていた。
	 */
	bool bSubpassOpaque = false;

	/**
	 * Masked のときに縁をディザ (確率的に抜く) で柔らかくするか。
	 *
	 * 無加工テクスチャは縁の画素が下地と未合成なので、硬いアルファテストで描くと
	 * その濃さがそのまま出る (IA では眉・目の輪郭の黒フチとして現れた)。
	 * 深度を書いたまま柔らかくする手段は UE ではディザしかないため、
	 * **無加工版を使う Masked 材質とディザはセット**にしてある。
	 */
	bool bDither = false;

	/**
	 * Masked のときのアルファのしきい値。
	 * 負の値 = アルファを見ない (glTF の alphaMode=OPAQUE)。
	 */
	float AlphaCutoff = 0.0f;
};

struct MMDPHYSICSEDITOR_API FMmdMaterialResult
{
	bool bSuccess = false;
	int32 Converted = 0;
	int32 Total = 0;
	/** そのうち Translucent マスターへ繋いだ数 (alphaMode=BLEND + origTexture 昇格組)。 */
	int32 Translucent = 0;
	/** そのうち origTexture により Masked から昇格した数。 */
	int32 Promoted = 0;
	/** そのうち「肌に貼り付けた半透明の材質」と判定して昇格した数 (眉・まつげ・額の影)。 */
	int32 Overlay = 0;
	/** BaseColorTex を無加工版へ差し替えられた数。 */
	int32 OrigTextureApplied = 0;
	/** GLB バイナリから取り出してアセット化したテクスチャ数。 */
	int32 ExtractedTextures = 0;
	FString Message;
};

class MMDPHYSICSEDITOR_API FMmdMaterialConversion
{
public:
	/**
	 * スケルタルメッシュのマテリアルを MMD トゥーンへ変換する。
	 *
	 * @param Mesh     UE 標準の Interchange glTF で取り込んだスケルタルメッシュ
	 * @param GlbPath  mmd2gltf-gui が出力した .glb (各マテリアルの extras.mmd を読む)
	 */
	static FMmdMaterialResult ConvertMaterials(USkeletalMesh* Mesh, const FString& GlbPath);

	/**
	 * 材質 1 つの変換方針を決める (アセットには触らない)。
	 *
	 * 移植元の renderQueue 2 帯構成を UE の描画パスに対応させたもの。詳しくは .cpp の注記を参照。
	 *   - 真の半透明 (alphaMode=BLEND / alphaClass="blend") → Translucent
	 *   - origTexture 由来でも alphaClass が blend でないもの → Masked のまま
	 *     (不透明パスで深度を書き、真の半透明より必ず先に描かれる)
	 */
	static FMmdMaterialPlan PlanMaterial(const MmdPhysics::MmdMaterialInfo& Info,
		const FMmdUvAlphaStats& UvStats = FMmdUvAlphaStats());

	/**
	 * スロット (材質) が使っている UV 領域だけを見て、無加工テクスチャのアルファ分布を測る。
	 * メッシュの編集用モデルとテクスチャのソース画像が要るのでエディタ専用。
	 * 測れなければ false (Samples=0 のまま)。
	 */
	static bool MeasureUvAlpha(USkeletalMesh* Mesh, int32 SlotIndex, UTexture2D* Texture,
		FMmdUvAlphaStats& OutStats);

	/**
	 * マスターマテリアルを用意する (無ければ生成する)。
	 * @param PackagePath 生成先 (例 "/Game/IA")
	 * @param Variant     Masked → "M_MmdToon" / Translucent → "M_MmdToonTranslucent"
	 */
	static UMaterial* EnsureMasterMaterial(const FString& PackagePath,
		EMmdMasterVariant Variant = EMmdMasterVariant::Masked);
};
