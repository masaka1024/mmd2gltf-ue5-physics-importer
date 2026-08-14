// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 依存ゼロの最小 JSON パーサ (GLB の glTF JSON / extras.mmd 読み取り用)。
//
// ★UE の FJsonSerializer に置き換えないこと。
//   移植元は 150 行の自作再帰下降パーサで、GlbPhysicsReader がその object/List/double という
//   素の表現に直接依存している。差し替えると読み取り側を全面的に書き直すことになり、
//   1:1 移植の追跡性が崩れる。挙動の同一性を優先してそのまま移植する。
//
// C# の object は Dictionary / List / string / double / bool / null のいずれかだった。
// C++ では型タグ付きの MmdJsonValue に置き換え、JSON の null と「キーが無い」は
// どちらも空の TSharedPtr で表す (移植元の null 相当)。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/MiniJson.cs (namespace BulletPhysics.Pmx)
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

namespace MmdPhysics
{
	enum class EMmdJsonType : uint8
	{
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	struct MmdJsonValue;

	using FMmdJsonObject = TMap<FString, TSharedPtr<MmdJsonValue>>;
	using FMmdJsonArray = TArray<TSharedPtr<MmdJsonValue>>;

	struct MMDPHYSICSCORE_API MmdJsonValue
	{
		EMmdJsonType Type = EMmdJsonType::Object;
		bool AsBool = false;
		double AsNumber = 0.0;
		FString AsString;
		FMmdJsonArray AsArray;
		FMmdJsonObject AsObject;
	};

	class MMDPHYSICSCORE_API MiniJson
	{
	public:
		static TSharedPtr<MmdJsonValue> Parse(const FString& s);

		// --- アクセス補助 (移植元の Obj/Arr/Str/Num/Int/Flt/Get に対応) ---
		static const FMmdJsonObject* Obj(const TSharedPtr<MmdJsonValue>& o);
		static const FMmdJsonArray* Arr(const TSharedPtr<MmdJsonValue>& o);
		static FString Str(const TSharedPtr<MmdJsonValue>& o);
		static double Num(const TSharedPtr<MmdJsonValue>& o);
		static int32 Int(const TSharedPtr<MmdJsonValue>& o);
		static float Flt(const TSharedPtr<MmdJsonValue>& o);

		static TSharedPtr<MmdJsonValue> Get(const FMmdJsonObject* o, const FString& Key);

	private:
		static TSharedPtr<MmdJsonValue> ParseValue(const FString& s, int32& i);
		static TSharedPtr<MmdJsonValue> ParseObject(const FString& s, int32& i);
		static TSharedPtr<MmdJsonValue> ParseArray(const FString& s, int32& i);
		static FString ParseString(const FString& s, int32& i);
		static TSharedPtr<MmdJsonValue> ParseNumber(const FString& s, int32& i);
		static void SkipWs(const FString& s, int32& i);
	};
}
