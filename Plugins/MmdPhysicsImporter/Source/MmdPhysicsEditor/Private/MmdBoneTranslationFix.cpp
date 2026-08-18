// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdBoneTranslationFix.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Misc/FileHelper.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPhysicsCoreLog.h"

#define LOCTEXT_NAMESPACE "MmdBoneTranslationFix"

using namespace MmdPhysics;

namespace
{
	/** root.extras.mmd。 */
	const FMmdJsonObject* GetExtrasMmd(const FMmdJsonObject* Root)
	{
		return MiniJson::Obj(MiniJson::Get(
			MiniJson::Obj(MiniJson::Get(Root, TEXT("extras"))), TEXT("mmd")));
	}

}

bool FMmdBoneTranslationFix::ReadBones(const FMmdJsonObject* Root, TArray<FMmdGlbBone>& OutBones,
	bool& bOutFromBonesArray)
{
	OutBones.Reset();
	bOutFromBonesArray = false;
	if (Root == nullptr) return false;

	// --- (1) 新しい .glb: extras.mmd.bones (PMX ボーン順) ---
	const FMmdJsonArray* Bones = MiniJson::Arr(MiniJson::Get(GetExtrasMmd(Root), TEXT("bones")));
	if (Bones != nullptr && Bones->Num() > 0)
	{
		for (const TSharedPtr<MmdJsonValue>& V : *Bones)
		{
			const FMmdJsonObject* B = MiniJson::Obj(V);
			FMmdGlbBone Out;
			if (B != nullptr)
			{
				Out.Name = MiniJson::Str(MiniJson::Get(B, TEXT("name")));
				Out.Flags = MiniJson::Int(MiniJson::Get(B, TEXT("flags")));
			}
			OutBones.Add(MoveTemp(Out));
		}
		bOutFromBonesArray = true;
		return true;
	}

	// --- (2) 古い .glb: skins[0].joints → nodes[n].extras.mmd.flags ---
	// ★ボーン番号の決め方は GlbPhysicsReader::BuildModel と同じにすること。
	//   剛体の `bone` はこの番号を指す。
	const FMmdJsonArray* Nodes = MiniJson::Arr(MiniJson::Get(Root, TEXT("nodes")));
	const FMmdJsonArray* Skins = MiniJson::Arr(MiniJson::Get(Root, TEXT("skins")));
	const FMmdJsonObject* Skin0 = (Skins != nullptr && Skins->Num() > 0) ? MiniJson::Obj((*Skins)[0]) : nullptr;
	const FMmdJsonArray* Joints = MiniJson::Arr(MiniJson::Get(Skin0, TEXT("joints")));
	if (Nodes == nullptr || Joints == nullptr) return false;

	bool bAnyFlags = false;
	for (const TSharedPtr<MmdJsonValue>& JV : *Joints)
	{
		const int32 Ni = MiniJson::Int(JV);
		const FMmdJsonObject* Node = Nodes->IsValidIndex(Ni) ? MiniJson::Obj((*Nodes)[Ni]) : nullptr;
		FMmdGlbBone Out;
		if (Node != nullptr)
		{
			Out.Name = MiniJson::Str(MiniJson::Get(Node, TEXT("name")));
			const FMmdJsonObject* Ex = MiniJson::Obj(MiniJson::Get(
				MiniJson::Obj(MiniJson::Get(Node, TEXT("extras"))), TEXT("mmd")));
			if (Ex != nullptr && Ex->Contains(TEXT("flags")))
			{
				Out.Flags = MiniJson::Int(MiniJson::Get(Ex, TEXT("flags")));
				bAnyFlags = true;
			}
		}
		OutBones.Add(MoveTemp(Out));
	}

	// フラグが 1 つも無い .glb では移動可かを決められない。触らない。
	if (!bAnyFlags)
	{
		OutBones.Reset();
		return false;
	}
	return true;
}

void FMmdBoneTranslationFix::CollectBoneCategories(const FMmdJsonObject* Root,
	const TArray<FMmdGlbBone>& Bones, TSet<FString>& OutMovable, TSet<FString>& OutPhysicsBaked)
{
	OutMovable.Reset();
	OutPhysicsBaked.Reset();

	// (a) 移動可ボーン。MMD が VMD の移動値を適用するのはここだけ。
	for (const FMmdGlbBone& B : Bones)
	{
		if (!B.Name.IsEmpty() && (B.Flags & KBoneFlagMovable) != 0) OutMovable.Add(B.Name);
	}

	// (b) 物理ベイクが位置を焼くボーン (mode 1:物理 / 2:物理+Bone合わせ)。
	//     並進そのものは正当だが、長さが保たれている間だけ (ヘッダの注記を参照)。
	const FMmdJsonArray* Rbs = MiniJson::Arr(MiniJson::Get(GetExtrasMmd(Root), TEXT("rigidBodies")));
	if (Rbs != nullptr)
	{
		for (const TSharedPtr<MmdJsonValue>& V : *Rbs)
		{
			const FMmdJsonObject* R = MiniJson::Obj(V);
			if (R == nullptr || !R->Contains(TEXT("bone"))) continue;
			if (MiniJson::Int(MiniJson::Get(R, TEXT("mode"))) == 0) continue;   // 0:追従は焼かない
			const int32 Bi = MiniJson::Int(MiniJson::Get(R, TEXT("bone")));
			if (Bones.IsValidIndex(Bi) && !Bones[Bi].Name.IsEmpty()) OutPhysicsBaked.Add(Bones[Bi].Name);
		}
	}
}

FMmdTranslationFixResult FMmdBoneTranslationFix::Apply(UAnimSequence* Anim, const FString& GlbPath)
{
	FMmdTranslationFixResult Result;

	if (Anim == nullptr)
	{
		Result.Message = TEXT("アニメーションが指定されていません。");
		return Result;
	}
	USkeleton* Skeleton = Anim->GetSkeleton();
	if (Skeleton == nullptr)
	{
		Result.Message = TEXT("アニメーションにスケルトンがありません。");
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

	TArray<FMmdGlbBone> Bones;
	if (!ReadBones(Root, Bones, Result.bFromBonesArray))
	{
		// 判定材料が無い .glb。推測で書き換えるより触らないほうが良い。
		Result.bSuccess = true;
		Result.Message = TEXT("この .glb にはボーンフラグが無いため、translation の検査はしません。");
		return Result;
	}
	Result.BonesRead = Bones.Num();

	TSet<FString> Movable, PhysicsBaked;
	CollectBoneCategories(Root, Bones, Movable, PhysicsBaked);
	TSet<FString> Known;
	for (const FMmdGlbBone& B : Bones)
	{
		if (!B.Name.IsEmpty()) Known.Add(B.Name);
	}

	IAnimationDataModel* Model = Anim->GetDataModel();
	if (Model == nullptr)
	{
		Result.Message = TEXT("アニメーションのデータモデルを取得できません。");
		return Result;
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();

	// --- 直す対象を先に洗い出す (ブラケットの中は書き込みだけにする) ---
	struct FPending
	{
		FName Name;
		TArray<FVector3f> Pos;
		TArray<FQuat4f> Rot;
		TArray<FVector3f> Scale;
		double OffsetCm = 0.0;
		/** 物理ベイク (mode 1/2) のボーンを焼き損じとして戻す場合の、最悪の長さ比。 */
		double LenRatio = 0.0;
	};
	TArray<FPending> Pending;

	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);
	for (const FName& TrackName : TrackNames)
	{
		// .glb に無い名前 (UE 側の仮想ボーン等) は判定材料が無いので触らない。
		if (!Known.Contains(TrackName.ToString()))
		{
			Result.TracksUnknown++;
			continue;
		}
		Result.TracksChecked++;
		// 移動可ボーンの並進は常に正当。ここで打ち切る。
		if (Movable.Contains(TrackName.ToString())) continue;
		const bool bBaked = PhysicsBaked.Contains(TrackName.ToString());

		const int32 BoneIdx = RefSkel.FindBoneIndex(TrackName);
		if (!RefPose.IsValidIndex(BoneIdx)) continue;
		const FTransform& Rest = RefPose[BoneIdx];
		const FVector RestPos = Rest.GetTranslation();

		// ★キーは FTransform で受け取る (生の PosKeys/RotKeys を触る API は 5.2 で
		//   非推奨になっている)。並進・回転・スケールが 1 本に揃って出てくるので、
		//   SetBoneTrackKeys が要求する「3 本とも同じ本数」も自動的に満たす。
		TArray<FTransform> Keys;
		Model->GetBoneTrackTransforms(TrackName, Keys);
		if (Keys.Num() == 0) continue;

		// ★読めなかったキーは FTransform::Identity で返ってくる
		//   (AnimDataModel.cpp の GetBoneTrackTransform: 並進・回転・スケールの
		//   どれか 1 本でも添字が足りないと Identity)。それを書き戻すと
		//   **回転まで潰す**ので、1 つでも混じっていたらこのトラックは触らない。
		//   保険なので「直せないなら何もしない」で良い。
		if (Keys.ContainsByPredicate([](const FTransform& T) { return T.Equals(FTransform::Identity); }))
		{
			UE_LOG(LogMmdPhysics, Warning,
				TEXT("[MmdPhysics] '%s' のキーを読み切れないため translation の検査を飛ばします。"),
				*TrackName.ToString());
			continue;
		}

		// 参照ポーズからどれだけ離れたか。丸め誤差だけなら触らない。
		double Worst = 0.0;
		for (const FTransform& T : Keys)
		{
			Worst = FMath::Max(Worst, FVector::Dist(T.GetTranslation(), RestPos));
		}
		if (Worst <= KOffsetEpsilonCm) continue;

		// ★物理ベイク (mode 1/2) の並進は、長さが保たれている限り正当なので触らない。
		//   参照ポーズ長から 2 倍 / 1/2 倍を外れたキーが 1 つでもあれば焼き損じ
		//   (旧エクスポーターの鎖の親取り違え)。詳しくはヘッダの注記。
		double LenRatio = 0.0;
		if (bBaked)
		{
			const double RestLen = RestPos.Size();
			if (RestLen <= KINDA_SMALL_NUMBER) continue;   // 親と同じ位置。比で見られない

			double WorstDev = 0.0;   // 1.0 からの外れ具合 (伸びも縮みも同じ土俵)
			for (const FTransform& T : Keys)
			{
				const double Ratio = T.GetTranslation().Size() / RestLen;
				const double Dev = FMath::Max(Ratio, Ratio > 0.0 ? 1.0 / Ratio : TNumericLimits<double>::Max());
				if (Dev > WorstDev) { WorstDev = Dev; LenRatio = Ratio; }
			}
			if (LenRatio < KBakedLenRatioMax && LenRatio > KBakedLenRatioMin) continue;
		}

		FPending P;
		P.Name = TrackName;
		P.OffsetCm = Worst;
		P.LenRatio = bBaked ? LenRatio : 0.0;
		P.Pos.Init(FVector3f(RestPos), Keys.Num());   // ★並進だけを参照ポーズで埋める
		P.Rot.Reserve(Keys.Num());
		P.Scale.Reserve(Keys.Num());
		for (const FTransform& T : Keys)
		{
			P.Rot.Add(FQuat4f(T.GetRotation()));       // 回転とスケールはそのまま戻す
			P.Scale.Add(FVector3f(T.GetScale3D()));
		}
		Pending.Add(MoveTemp(P));
	}

	if (Pending.Num() == 0)
	{
		Result.bSuccess = true;
		Result.Message = FString::Printf(
			TEXT("移動できないボーンの translation はありませんでした ")
			TEXT("(ボーン %d / 調べたトラック %d / 判定外 %d / フラグの出所: %s)"),
			Result.BonesRead, Result.TracksChecked, Result.TracksUnknown,
			Result.bFromBonesArray ? TEXT("extras.mmd.bones") : TEXT("nodes[].extras.mmd.flags"));
		UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
		return Result;
	}

	// ★書き換えはブラケットで囲む。SetBoneTrackKeys は 1 回ごとに変更通知を出し、
	//   その度に**アニメーション全体の再圧縮**が走る (モーフカーブの追加と同じ事情。
	//   詳しくは MmdMorphAnimation.h の注記)。
	//   トランザクション化はしない (コマンドレットからも呼ぶため)。
	{
		IAnimationDataController& Controller = Anim->GetController();
		IAnimationDataController::FScopedBracket Bracket(
			Controller, LOCTEXT("MmdFixTranslation", "移動できないボーンの translation を戻す"),
			/*bShouldTransact=*/false);

		for (const FPending& P : Pending)
		{
			if (!Controller.SetBoneTrackKeys(P.Name, P.Pos, P.Rot, P.Scale, /*bShouldTransact=*/false))
			{
				UE_LOG(LogMmdPhysics, Warning,
					TEXT("[MmdPhysics] '%s' の translation を戻せませんでした。"), *P.Name.ToString());
				continue;
			}
			Result.TracksReset++;
			Result.KeysReset += P.Pos.Num();
			Result.WorstOffsetCm = FMath::Max(Result.WorstOffsetCm, P.OffsetCm);
			Result.ResetBones.Add(P.Name.ToString());
			if (P.LenRatio > 0.0)
			{
				Result.TracksResetBaked++;
				// 1.0 から遠いほうを残す (伸びと縮みが混ざるため)。
				if (FMath::Abs(FMath::Loge(P.LenRatio)) > FMath::Abs(FMath::Loge(Result.WorstLenRatio)))
				{
					Result.WorstLenRatio = P.LenRatio;
				}
			}
		}
	}

	Result.bSuccess = true;

	// 焼き損じ (mode 1/2) を戻した分は別に出す。原因が別 (鎖の親取り違え) なので、
	// 「どちらの壊れ方だったか」がログから分かるようにしておく。
	const FString BakedNote = Result.TracksResetBaked > 0
		? FString::Printf(TEXT(" / うち物理ベイクの焼き損じ %d 本 (最悪 %.2f 倍)"),
			Result.TracksResetBaked, Result.WorstLenRatio)
		: FString();
	Result.Message = FString::Printf(
		TEXT("移動できないボーンの translation を参照ポーズへ戻しました: %d トラック ")
		TEXT("(キー %d / 最大ずれ %.2f cm%s / 調べたトラック %d / 判定外 %d / フラグの出所: %s)"),
		Result.TracksReset, Result.KeysReset, Result.WorstOffsetCm, *BakedNote,
		Result.TracksChecked, Result.TracksUnknown,
		Result.bFromBonesArray ? TEXT("extras.mmd.bones") : TEXT("nodes[].extras.mmd.flags"));

	// ★直したこと自体を必ず見せる。ここが 0 でないのは「.glb が古い」という意味で、
	//   本来は出し直してもらうのが正しい。黙って直すと古い .glb を使い続けてしまう。
	UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] %s"), *Result.Message);
	UE_LOG(LogMmdPhysics, Warning,
		TEXT("[MmdPhysics] 対象ボーン: %s"), *FString::Join(Result.ResetBones, TEXT(", ")));
	UE_LOG(LogMmdPhysics, Warning,
		TEXT("[MmdPhysics] これは mmd2gltf-gui の aa5cc7b (2026-08-17) より前の .glb にだけ")
		TEXT("起きます。出し直せるならエクスポーターを新しくして出し直してください。"));
	return Result;
}

#undef LOCTEXT_NAMESPACE
