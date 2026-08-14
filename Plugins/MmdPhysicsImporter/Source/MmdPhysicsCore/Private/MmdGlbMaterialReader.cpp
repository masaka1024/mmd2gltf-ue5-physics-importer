// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdGlbMaterialReader.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdMiniJson.h"
#include "Misc/FileHelper.h"

namespace MmdPhysics
{
	namespace
	{
		void ReadFloats(const TSharedPtr<MmdJsonValue>& Node, float* Out, int32 Count)
		{
			const FMmdJsonArray* a = MiniJson::Arr(Node);
			if (a == nullptr) return;
			for (int32 i = 0; i < Count && i < a->Num(); i++) Out[i] = MiniJson::Flt((*a)[i]);
		}

		/** キーが存在すればその値、無ければ Fallback。extras.mmd は欠損し得るので既定値を明示する。 */
		int32 IntOr(const FMmdJsonObject* Obj, const TCHAR* Key, int32 Fallback)
		{
			const TSharedPtr<MmdJsonValue> V = MiniJson::Get(Obj, Key);
			return V.IsValid() ? MiniJson::Int(V) : Fallback;
		}
	}

	bool GlbMaterialReader::LoadFile(const FString& Path, MmdMaterialSet& OutSet, TArray<FString>& OutWarnings)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			OutWarnings.Add(FString::Printf(TEXT("GLB を読めない: %s"), *Path));
			return false;
		}
		return LoadBytes(Bytes, OutSet, OutWarnings);
	}

	bool GlbMaterialReader::LoadBytes(const TArray<uint8>& Glb, MmdMaterialSet& OutSet, TArray<FString>& OutWarnings)
	{
		TSharedPtr<MmdJsonValue> Root;
		TArray<uint8> Bin;
		if (!GlbPhysicsReader::ParseGlb(Glb, Root, Bin, OutWarnings)) return false;

		const FMmdJsonObject* root = MiniJson::Obj(Root);

		// --- glTF texture index → 画像名 ---
		{
			const FMmdJsonArray* Textures = MiniJson::Arr(MiniJson::Get(root, TEXT("textures")));
			const FMmdJsonArray* Images = MiniJson::Arr(MiniJson::Get(root, TEXT("images")));
			if (Textures != nullptr)
			{
				OutSet.TextureImageNames.SetNum(Textures->Num());
				for (int32 t = 0; t < Textures->Num(); t++)
				{
					const FMmdJsonObject* Tex = MiniJson::Obj((*Textures)[t]);
					const int32 Source = IntOr(Tex, TEXT("source"), -1);
					if (Images != nullptr && Images->IsValidIndex(Source))
					{
						OutSet.TextureImageNames[t] = MiniJson::Str(MiniJson::Get(MiniJson::Obj((*Images)[Source]), TEXT("name")));
					}
				}
			}
		}

		// --- マテリアル ---
		const FMmdJsonArray* Materials = MiniJson::Arr(MiniJson::Get(root, TEXT("materials")));
		if (Materials == nullptr)
		{
			OutWarnings.Add(TEXT("glTF に materials がありません。"));
			return false;
		}

		int32 WithMmdExtras = 0;
		for (int32 i = 0; i < Materials->Num(); i++)
		{
			const FMmdJsonObject* M = MiniJson::Obj((*Materials)[i]);
			MmdMaterialInfo Info;
			Info.Name = MiniJson::Str(MiniJson::Get(M, TEXT("name")));

			// glTF 標準側
			{
				const FMmdJsonObject* Pbr = MiniJson::Obj(MiniJson::Get(M, TEXT("pbrMetallicRoughness")));
				const FMmdJsonObject* BaseTex = MiniJson::Obj(MiniJson::Get(Pbr, TEXT("baseColorTexture")));
				Info.BaseColorTexture = IntOr(BaseTex, TEXT("index"), -1);
				const TSharedPtr<MmdJsonValue> DS = MiniJson::Get(M, TEXT("doubleSided"));
				Info.bDoubleSided = DS.IsValid() && DS->Type == EMmdJsonType::Bool && DS->AsBool;
				Info.AlphaMode = MiniJson::Str(MiniJson::Get(M, TEXT("alphaMode")));
			}

			// extras.mmd 側
			const FMmdJsonObject* Ex = MiniJson::Obj(MiniJson::Get(MiniJson::Obj(MiniJson::Get(M, TEXT("extras"))), TEXT("mmd")));
			if (Ex != nullptr)
			{
				WithMmdExtras++;
				Info.NameEn = MiniJson::Str(MiniJson::Get(Ex, TEXT("nameEn")));
				ReadFloats(MiniJson::Get(Ex, TEXT("ambient")), Info.Ambient, 3);
				ReadFloats(MiniJson::Get(Ex, TEXT("specular")), Info.Specular, 3);
				Info.SpecularPower = MiniJson::Flt(MiniJson::Get(Ex, TEXT("specularPower")));
				Info.Flags = IntOr(Ex, TEXT("flags"), 0);
				ReadFloats(MiniJson::Get(Ex, TEXT("edgeColor")), Info.EdgeColor, 4);
				Info.EdgeSize = MiniJson::Flt(MiniJson::Get(Ex, TEXT("edgeSize")));
				Info.SphereMode = IntOr(Ex, TEXT("sphereMode"), 0);
				// テクスチャ index は「無し」が -1 なので、キー欠損時も -1 を既定にする。
				Info.SphereTexture = IntOr(Ex, TEXT("sphereTexture"), -1);
				Info.ToonTexture = IntOr(Ex, TEXT("toonTexture"), -1);
				Info.ToonShared = IntOr(Ex, TEXT("toonShared"), -1);
				Info.AlphaClass = MiniJson::Str(MiniJson::Get(Ex, TEXT("alphaClass")));
				Info.OrigTexture = IntOr(Ex, TEXT("origTexture"), -1);
			}

			OutSet.Materials.Add(Info);
		}

		if (WithMmdExtras == 0)
		{
			OutWarnings.Add(TEXT("どのマテリアルにも extras.mmd がありません。")
				TEXT("mmd2gltf-gui が出力した .glb を指定してください")
				TEXT("(一般の glTF エクスポータの出力にはトゥーン/スフィアの情報がありません)。"));
			return false;
		}
		if (WithMmdExtras < Materials->Num())
		{
			OutWarnings.Add(FString::Printf(TEXT("extras.mmd を持つマテリアルは %d / %d です。"),
				WithMmdExtras, Materials->Num()));
		}
		return true;
	}
}
