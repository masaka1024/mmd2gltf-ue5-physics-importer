// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// GLB から MMD マテリアル情報を取り出すリーダ。
//
// ★物理 (extras.mmd.rigidBodies) と違い、マテリアル情報は
//   **glTF の各マテリアルの extras.mmd** に入っている。ルートの extras.mmd には無い。
//   実データ (IA.glb) で確認した構造:
//
//     materials[i] = {
//       name, doubleSided, alphaMode,
//       pbrMetallicRoughness: { baseColorTexture: { index } },
//       extensions: { KHR_materials_unlit: {} },
//       extras: { mmd: {
//         nameEn, ambient[3], specular[3], specularPower, flags,
//         edgeColor[4], edgeSize, sphereMode, sphereTexture,
//         toonTexture, toonShared, memo, alphaClass, origTexture
//       }}
//     }
//
//   MMD のマテリアルは KHR_materials_unlit 付きで出力される。
//
// 読み取り専用。物理には一切関与しない。
// ===========================================================================

#pragma once

#include "MmdMathTypes.h"

namespace MmdPhysics
{
	/** PMX マテリアルの生パラメータ (PMX 仕様 ●材質)。 */
	struct MmdMaterialInfo
	{
		FString Name;                 // glTF material の name (UE 側のマテリアル名の元になる)
		FString NameEn;

		// --- glTF 側 ---
		int32 BaseColorTexture = -1;  // glTF texture index (-1 = 無し)

		/**
		 * PMX の材質の拡散色 (RGBA)。glTF の pbrMetallicRoughness.baseColorFactor。
		 *
		 * ★テクスチャを持たない材質 (レンズ・金属パーツ・単色のベルト等) は
		 *   **ここにしか色が無い**。既定を白にしてあるので、キーが無ければ素通しになる。
		 *   アルファも入っており、MMD の半透明パーツ (レンズの 0.7 等) はこの値で決まる。
		 */
		float BaseColorFactor[4] = { 1, 1, 1, 1 };

		bool bDoubleSided = false;
		FString AlphaMode;            // OPAQUE / MASK / BLEND

		/**
		 * glTF の alphaCutoff (alphaMode=MASK のときのしきい値)。
		 * キーが無ければ 0 のまま。実際に使う値は EffectiveAlphaCutoff() を通すこと。
		 */
		float AlphaCutoff = 0.0f;

		// --- extras.mmd 側 (PMX 生値) ---
		float Ambient[3] = { 0, 0, 0 };
		float Specular[3] = { 0, 0, 0 };
		float SpecularPower = 0.0f;

		/**
		 * PMX 描画フラグ。
		 * 0x01 両面描画 / 0x02 地面影 / 0x04 セルフシャドウマップへの描画
		 * 0x08 セルフシャドウの描画 / 0x10 エッジ描画
		 */
		int32 Flags = 0;

		float EdgeColor[4] = { 0, 0, 0, 1 };
		float EdgeSize = 0.0f;

		/** 0=無効 1=乗算 2=加算 3=サブテクスチャ */
		int32 SphereMode = 0;
		int32 SphereTexture = -1;     // glTF texture index (-1 = 無し)

		int32 ToonTexture = -1;       // glTF texture index (-1 = 無し)
		int32 ToonShared = -1;        // 共有トゥーン 0..9 (-1 = 個別 or 無し)

		/**
		 * 本来のα分類。エクスポーターがプリベイク前の元テクスチャを見て付ける。
		 * 実測値は "opaque" / "mask" / "blend"、古い .glb では未記載 (空)。
		 *
		 * ★alphaMode (glTF 標準) はプリベイク後の**ビューア安全な**値であり、
		 *   MMD 本来の透け具合はこちらにしか残っていない。
		 *   "blend" = 真の半透明 (透け髪・髪影・チーク・柔らかい眉)。
		 *   未記載や "mask" はカットアウト由来 (肌・服・メガネ等で見た目はほぼ不透明)。
		 */
		FString AlphaClass;

		/**
		 * プリベイク前の無加工テクスチャの glTF texture index (-1 = 無し)。
		 * エクスポーターはビューア安全のため α を 0/255 に平坦化した版を
		 * baseColorTexture に使い、無加工版を別テクスチャとして同梱する。
		 * ★この画像は標準マテリアルからは参照されないため、UE の Interchange では
		 *   アセット化されない。GlbImageExtractor で GLB から直接取り出す。
		 */
		int32 OrigTexture = -1;

		bool HasEdge() const { return (Flags & 0x10) != 0; }
		bool IsDoubleSidedFlag() const { return (Flags & 0x01) != 0; }

		/**
		 * alphaMode=MASK で使うしきい値。
		 * glTF の既定は 0.5。キー欠損 (=0) を 0.5 に読み替えるのは移植元と同じ扱い
		 * (移植元 `md.alphaCutoff > 0 ? md.alphaCutoff : 0.5f`)。
		 */
		float EffectiveAlphaCutoff() const { return AlphaCutoff > 0.0f ? AlphaCutoff : 0.5f; }

		/** extras.mmd の alphaClass が "blend" (= 真の半透明) か。 */
		bool IsAlphaClassBlend() const { return AlphaClass.Equals(TEXT("blend"), ESearchCase::IgnoreCase); }
	};

	/** GLB から取り出したマテリアル一式。 */
	struct MmdMaterialSet
	{
		TArray<MmdMaterialInfo> Materials;

		/**
		 * glTF texture index → その画像の名前 (images[source].name)。
		 * UE 側で Interchange が取り込んだ UTexture2D を名前で引き当てるのに使う。
		 * 例: "眼球４.bmp" → アセット名 "眼球４_bmp"
		 */
		TArray<FString> TextureImageNames;

		/** glTF texture index が有効で画像名が取れるか。 */
		bool HasTexture(int32 TextureIndex) const
		{
			return TextureImageNames.IsValidIndex(TextureIndex) && !TextureImageNames[TextureIndex].IsEmpty();
		}
	};

	class MMDPHYSICSCORE_API GlbMaterialReader
	{
	public:
		/** GLB からマテリアル情報を読む。読めなければ false (警告は OutWarnings へ)。 */
		static bool LoadFile(const FString& Path, MmdMaterialSet& OutSet, TArray<FString>& OutWarnings);
		static bool LoadBytes(const TArray<uint8>& Glb, MmdMaterialSet& OutSet, TArray<FString>& OutWarnings);
	};

	/** GLB の BIN チャンクから切り出した画像 1 枚分の生バイト列。 */
	struct MmdGlbImage
	{
		FString Name;          // images[i].name (無ければ空)
		FString MimeType;      // "image/png" / "image/jpeg"
		TArray<uint8> Bytes;   // PNG / JPEG のファイルそのもの

		/**
		 * UE のテクスチャファクトリへ渡す拡張子 ("png" / "jpg")。
		 * mimeType が無い .glb もあるので、無ければ先頭バイトで判別する。
		 */
		MMDPHYSICSCORE_API FString Extension() const;
	};

	/**
	 * GLB の textures → images → bufferViews を辿って、生画像バイト列を切り出す。
	 *
	 * ★これが要る理由: エクスポーターは toon / sphere / origTexture の画像を GLB に
	 *   同梱しているが、glTF の標準マテリアルからは参照していない。UE の Interchange は
	 *   参照されていない画像をアセット化しないため、名前で探しても見つからない。
	 *   移植元 Unity 版も UniGLTF が同じ挙動だったため、GLB バイナリから自前で
	 *   取り出していた (PrepareGlbBinaryAccess / ExtractTextureFromGlb)。
	 *
	 * 移植元は chunk0(JSON) 長から chunk1(BIN) 先頭を 20 + chunk0Length と直接計算していたが、
	 * こちらは既にある GlbPhysicsReader::ParseGlb でチャンクを走査して BIN を取り出す
	 * (チャンクの並び順を仮定しないぶん安全で、物理側と同じ経路になる)。
	 */
	class MMDPHYSICSCORE_API GlbImageExtractor
	{
	public:
		/** GLB を読み込んで textures/images/bufferViews と BIN チャンクを控える。 */
		bool Prepare(const FString& Path, TArray<FString>& OutWarnings);
		bool PrepareBytes(const TArray<uint8>& Glb, TArray<FString>& OutWarnings);

		bool IsReady() const { return bReady; }
		int32 NumTextures() const { return TextureSources.Num(); }

		/** glTF texture index の画像を切り出す。範囲外・BIN 外なら false (警告は OutWarnings へ)。 */
		bool ExtractTexture(int32 TextureIndex, MmdGlbImage& OutImage, TArray<FString>& OutWarnings) const;

	private:
		struct FImageRef
		{
			FString Name;
			FString MimeType;
			int32 BufferView = -1;
		};
		struct FBufferViewRef
		{
			int32 ByteOffset = 0;
			int32 ByteLength = 0;
		};

		bool bReady = false;
		TArray<int32> TextureSources;      // textures[i].source
		TArray<FImageRef> Images;
		TArray<FBufferViewRef> BufferViews;
		TArray<uint8> Bin;                 // BIN チャンク (bufferView.byteOffset の基準)
	};
}
