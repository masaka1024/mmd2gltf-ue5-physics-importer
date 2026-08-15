// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdMorphAnimation.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdMiniJson.h"
#include "MmdPhysicsCoreLog.h"

#define LOCTEXT_NAMESPACE "MmdMorphAnimation"

using namespace MmdPhysics;

namespace
{
	/** glTF の componentType。FLOAT 以外のアクセサは読まない (正規化整数は想定外)。 */
	constexpr int32 KComponentTypeFloat = 5126;

	/**
	 * 最初に見つかった weights チャンネルを返す。
	 *
	 * MMD の変換では対象メッシュは 1 つで、モーフの weights チャンネルも 1 本しか出ない
	 * (実測: IA は channels 55 のうち rotation 53 / translation 1 / weights 1)。
	 * 複数あるモデルは想定していないので、最初の 1 本だけ扱う。
	 */
	bool FindWeightsChannel(const FMmdJsonObject* Root, int32& OutMeshIndex,
		int32& OutInputAcc, int32& OutOutputAcc, FString& OutInterpolation, FString& OutAnimName)
	{
		const FMmdJsonArray* Animations = MiniJson::Arr(MiniJson::Get(Root, TEXT("animations")));
		if (Animations == nullptr) return false;

		const FMmdJsonArray* Nodes = MiniJson::Arr(MiniJson::Get(Root, TEXT("nodes")));
		if (Nodes == nullptr) return false;

		for (const TSharedPtr<MmdJsonValue>& AnimVal : *Animations)
		{
			const FMmdJsonObject* Anim = MiniJson::Obj(AnimVal);
			if (Anim == nullptr) continue;
			const FMmdJsonArray* Channels = MiniJson::Arr(MiniJson::Get(Anim, TEXT("channels")));
			const FMmdJsonArray* Samplers = MiniJson::Arr(MiniJson::Get(Anim, TEXT("samplers")));
			if (Channels == nullptr || Samplers == nullptr) continue;

			for (const TSharedPtr<MmdJsonValue>& ChVal : *Channels)
			{
				const FMmdJsonObject* Ch = MiniJson::Obj(ChVal);
				if (Ch == nullptr) continue;
				const FMmdJsonObject* Target = MiniJson::Obj(MiniJson::Get(Ch, TEXT("target")));
				if (Target == nullptr) continue;
				if (MiniJson::Str(MiniJson::Get(Target, TEXT("path"))) != TEXT("weights")) continue;

				// target.node → nodes[n].mesh (glTF 仕様上、weights はメッシュを持つノードしか指せない)
				const int32 NodeIdx = MiniJson::Int(MiniJson::Get(Target, TEXT("node")));
				if (!Nodes->IsValidIndex(NodeIdx)) continue;
				const FMmdJsonObject* Node = MiniJson::Obj((*Nodes)[NodeIdx]);
				if (Node == nullptr || !Node->Contains(TEXT("mesh"))) continue;

				const int32 SamplerIdx = MiniJson::Int(MiniJson::Get(Ch, TEXT("sampler")));
				if (!Samplers->IsValidIndex(SamplerIdx)) continue;
				const FMmdJsonObject* Sampler = MiniJson::Obj((*Samplers)[SamplerIdx]);
				if (Sampler == nullptr) continue;

				OutMeshIndex = MiniJson::Int(MiniJson::Get(Node, TEXT("mesh")));
				OutInputAcc = MiniJson::Int(MiniJson::Get(Sampler, TEXT("input")));
				OutOutputAcc = MiniJson::Int(MiniJson::Get(Sampler, TEXT("output")));
				OutInterpolation = Sampler->Contains(TEXT("interpolation"))
					? MiniJson::Str(MiniJson::Get(Sampler, TEXT("interpolation"))) : TEXT("LINEAR");
				OutAnimName = MiniJson::Str(MiniJson::Get(Anim, TEXT("name")));
				return true;
			}
		}
		return false;
	}

	/** accessor の componentType が FLOAT か。 */
	bool IsFloatAccessor(const FMmdJsonObject* Root, int32 AccIdx)
	{
		const FMmdJsonArray* Accessors = MiniJson::Arr(MiniJson::Get(Root, TEXT("accessors")));
		if (Accessors == nullptr || !Accessors->IsValidIndex(AccIdx)) return false;
		const FMmdJsonObject* Acc = MiniJson::Obj((*Accessors)[AccIdx]);
		if (Acc == nullptr) return false;
		return MiniJson::Int(MiniJson::Get(Acc, TEXT("componentType"))) == KComponentTypeFloat;
	}

	/** meshes[i].extras.targetNames。UE がモーフ名を読むのと同じ場所。 */
	TArray<FString> ReadTargetNames(const FMmdJsonObject* Root, int32 MeshIndex)
	{
		TArray<FString> Names;
		const FMmdJsonArray* Meshes = MiniJson::Arr(MiniJson::Get(Root, TEXT("meshes")));
		if (Meshes == nullptr || !Meshes->IsValidIndex(MeshIndex)) return Names;
		const FMmdJsonObject* Mesh = MiniJson::Obj((*Meshes)[MeshIndex]);
		if (Mesh == nullptr) return Names;
		const FMmdJsonObject* Extras = MiniJson::Obj(MiniJson::Get(Mesh, TEXT("extras")));
		if (Extras == nullptr) return Names;
		const FMmdJsonArray* TargetNames = MiniJson::Arr(MiniJson::Get(Extras, TEXT("targetNames")));
		if (TargetNames == nullptr) return Names;
		for (const TSharedPtr<MmdJsonValue>& V : *TargetNames)
		{
			Names.Add(MiniJson::Str(V));
		}
		return Names;
	}
}

FMmdMorphAnimResult FMmdMorphAnimation::ApplyMorphCurves(USkeletalMesh* Mesh, UAnimSequence* Anim,
	const FString& GlbPath)
{
	FMmdMorphAnimResult Result;

	if (Mesh == nullptr || Anim == nullptr)
	{
		Result.Message = TEXT("メッシュかアニメーションが指定されていません。");
		return Result;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *GlbPath))
	{
		Result.Message = FString::Printf(TEXT(".glb を読めません: %s"), *GlbPath);
		return Result;
	}

	TSharedPtr<MmdJsonValue> RootVal;
	TArray<uint8> Bin;
	TArray<FString> Warnings;
	if (!GlbPhysicsReader::ParseGlb(Bytes, RootVal, Bin, Warnings))
	{
		for (const FString& W : Warnings) UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *W);
		Result.Message = TEXT(".glb を解析できません。");
		return Result;
	}
	const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
	if (Root == nullptr)
	{
		Result.Message = TEXT(".glb の JSON チャンクが不正です。");
		return Result;
	}

	int32 MeshIndex = -1, InputAcc = -1, OutputAcc = -1;
	FString Interpolation, AnimName;
	if (!FindWeightsChannel(Root, MeshIndex, InputAcc, OutputAcc, Interpolation, AnimName))
	{
		// モーション無しの .glb、あるいはモーフキーを持たないモーション。異常ではない。
		Result.bSuccess = true;
		Result.Message = TEXT("この .glb にはモーフのアニメーションがありません。");
		return Result;
	}

	if (!IsFloatAccessor(Root, InputAcc) || !IsFloatAccessor(Root, OutputAcc))
	{
		// 正規化整数のウェイトは glTF 仕様上あり得るが、エクスポーターは float で出す。
		Result.Message = TEXT("モーフのアクセサが FLOAT ではありません (未対応)。");
		return Result;
	}

	const TArray<FString> TargetNames = ReadTargetNames(Root, MeshIndex);
	if (TargetNames.Num() == 0)
	{
		Result.Message = TEXT("mesh.extras.targetNames がありません (モーフ名を決められません)。");
		return Result;
	}
	Result.TotalTracks = TargetNames.Num();

	const TArray<float> Times = GlbPhysicsReader::ReadFloatAccessor(RootVal, Bin, InputAcc);
	const TArray<float> Values = GlbPhysicsReader::ReadFloatAccessor(RootVal, Bin, OutputAcc);
	if (Times.Num() == 0)
	{
		Result.Message = TEXT("モーフのキー時刻を読めません。");
		return Result;
	}

	// ★並びは「フレームごとに全トラック」: t0 の [track0..trackN-1]、t1 の [track0..trackN-1] …
	//   (glTF 仕様。CUBICSPLINE のときは 1 キーにつき in/value/out の 3 つが並ぶ)
	const int32 Stride = (Interpolation == TEXT("CUBICSPLINE")) ? 3 : 1;
	const int64 Expected = static_cast<int64>(Times.Num()) * TargetNames.Num() * Stride;
	if (Values.Num() < Expected)
	{
		Result.Message = FString::Printf(
			TEXT("モーフのウェイト数が足りません (%d < %lld)。"), Values.Num(), Expected);
		return Result;
	}

	USkeleton* Skeleton = Anim->GetSkeleton();

	// 既にあるカーブ名を一度だけ集めておく (トラックごとに問い合わせない)。
	TArray<FName> ExistingCurves;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(Anim, ERawCurveTrackTypes::RCT_Float, ExistingCurves);

	// ★カーブの追加はブラケットで囲む。
	//   AddCurve / AddFloatCurveKeys は 1 回ごとにアニメーションデータの変更通知を出し、
	//   その度に**アニメーション全体の再圧縮**が走る。IA (54 トラック × 7000 フレーム、
	//   圧縮データ 72MB) に 37 本入れると数十分かかって実質固まる。
	//   ブラケットで囲むと通知が閉じたときの 1 回にまとまる。
	//   トランザクション化はしない (bShouldTransact=false)。エディタの Undo に
	//   数万キー分を積む意味が無く、コマンドレット (自動テスト) からも呼ぶため。
	IAnimationDataController::FScopedBracket Bracket(
		Anim->GetController(), LOCTEXT("MmdMorphCurves", "MMD のモーフカーブを追加"),
		/*bShouldTransact=*/false);

	// トラックごとにカーブを 1 本作る。
	TArray<float> KeyTimes;
	TArray<float> KeyValues;
	for (int32 Track = 0; Track < TargetNames.Num(); Track++)
	{
		const FName CurveName(*TargetNames[Track]);

		// ★メッシュに同名のモーフターゲットが無いものは飛ばす。
		//   UV モーフ (PMX のモーフ種別 3) は UE のモーフターゲットに対応概念が無く
		//   Interchange も取り込まないので、カーブだけ作っても何も動かない。
		if (Mesh->FindMorphTarget(CurveName) == nullptr)
		{
			Result.SkippedNoMorphTarget++;
			continue;
		}

		// 既にカーブがあるなら触らない (UE 側が直った場合に二重で入れないため)。
		if (ExistingCurves.Contains(CurveName))
		{
			Result.SkippedExisting++;
			continue;
		}

		KeyTimes.Reset(Times.Num());
		KeyValues.Reset(Times.Num());
		for (int32 Key = 0; Key < Times.Num(); Key++)
		{
			// CUBICSPLINE は [in, value, out] の真ん中が値。接線は捨てる
			// (Interchange の morph curve 経路も同じく捨てている)。
			const int64 At = (static_cast<int64>(Key) * TargetNames.Num() + Track) * Stride
				+ (Stride == 3 ? 1 : 0);
			KeyTimes.Add(Times[Key]);
			KeyValues.Add(Values[static_cast<int32>(At)]);
		}

		UAnimationBlueprintLibrary::AddCurve(Anim, CurveName, ERawCurveTrackTypes::RCT_Float,
			/*bMetaDataCurve=*/false);
		UAnimationBlueprintLibrary::AddFloatCurveKeys(Anim, CurveName, KeyTimes, KeyValues);

		// ★スケルトン側に「これはモーフを動かすカーブだ」と登録する。
		//   FBX インポータ (SkeletalMeshEdit.cpp) と Interchange の AnimSequenceFactory が
		//   同じことをしている。これが無いと、カーブはあるのにモーフが動かないことがある。
		if (Skeleton != nullptr)
		{
			Skeleton->AccumulateCurveMetaData(CurveName, /*bMaterialSet=*/false, /*bMorphtargetSet=*/true);
		}

		Result.CurvesAdded++;
		Result.KeysAdded += KeyTimes.Num();
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("モーフのアニメーションを追加しました: %d / %d トラック (キー %d / ")
		TEXT("モーフターゲット無しで除外 %d / 既存のまま %d)"),
		Result.CurvesAdded, Result.TotalTracks, Result.KeysAdded,
		Result.SkippedNoMorphTarget, Result.SkippedExisting);
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

#undef LOCTEXT_NAMESPACE
