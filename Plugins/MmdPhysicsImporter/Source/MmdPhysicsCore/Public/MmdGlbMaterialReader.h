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
		bool bDoubleSided = false;
		FString AlphaMode;            // OPAQUE / MASK / BLEND

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

		FString AlphaClass;
		int32 OrigTexture = -1;

		bool HasEdge() const { return (Flags & 0x10) != 0; }
		bool IsDoubleSidedFlag() const { return (Flags & 0x01) != 0; }
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
}
