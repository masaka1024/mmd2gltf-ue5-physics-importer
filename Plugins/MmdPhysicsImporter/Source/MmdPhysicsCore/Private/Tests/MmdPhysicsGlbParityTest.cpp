// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証A: 移植元 C# エンジンとの数値パリティ。
//
// 実モデルの GLB は再配布できないため、テストデータは環境変数で外から与える。
// 変数が無ければテストは何もせず成功する (データを持たない環境で赤くしないため)。
//
//   MMD_PARITY_GLB     … mmd2gltf-gui が出力した .glb
//   MMD_PARITY_CSV     … Tools/CsReference が出力した基準 CSV
//   MMD_PARITY_FRAMES  … ステップ数 (既定 60。CSV を作ったときと同じ値にすること)
//   MMD_PARITY_TOL     … 位置の許容差 (PMX 単位。既定 1e-3 = 0.08mm 相当)
//
// 基準 CSV の作り方:
//   dotnet run --project Tools/CsReference -c Release -- <glb> 60 out/ia_60_cs.csv
//
// ★駆動は行わない。アニメーションを与えると取り込み経路の差まで混ざり、
//   物理エンジンの移植が正しいかを切り分けられなくなる。両側とも
//   「AddBody がバインド姿勢で初期化した KinematicTarget のまま StepSimulation を回す」。

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPmxPhysicsBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	struct FGoldenRow
	{
		FString Name;
		Vec3 Pos;
		Quat Rot;
	};

	bool ParseGolden(const FString& CsvText, TArray<FGoldenRow>& OutRows, FString& OutError)
	{
		TArray<FString> Lines;
		CsvText.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 2) { OutError = TEXT("CSV の行が足りない"); return false; }
		// 1 行目はヘッダ。
		for (int32 L = 1; L < Lines.Num(); L++)
		{
			const FString& Line = Lines[L];
			if (Line.IsEmpty()) continue;
			TArray<FString> Cols;
			Line.ParseIntoArray(Cols, TEXT(","), false);
			if (Cols.Num() < 9)
			{
				OutError = FString::Printf(TEXT("%d 行目の列数が足りない (%d)"), L + 1, Cols.Num());
				return false;
			}
			FGoldenRow Row;
			Row.Name = Cols[1];
			Row.Pos = Vec3(FCString::Atof(*Cols[2]), FCString::Atof(*Cols[3]), FCString::Atof(*Cols[4]));
			Row.Rot = Quat(FCString::Atof(*Cols[5]), FCString::Atof(*Cols[6]),
				FCString::Atof(*Cols[7]), FCString::Atof(*Cols[8]));
			OutRows.Add(Row);
		}
		return true;
	}

	/** クォータニオンは q と -q が同一回転なので、符号を揃えてから比較する。 */
	float QuatDelta(const Quat& A, const Quat& B)
	{
		const float Dot = A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w;
		const float s = Dot < 0.0f ? -1.0f : 1.0f;
		const float dx = A.x - s * B.x, dy = A.y - s * B.y, dz = A.z - s * B.z, dw = A.w - s * B.w;
		return MSqrt(dx * dx + dy * dy + dz * dz + dw * dw);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPhysicsGlbParityTest, "MmdPhysics.Core.GlbParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdPhysicsGlbParityTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
	const FString CsvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_CSV"));
	if (GlbPath.IsEmpty() || CsvPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PARITY_GLB / MMD_PARITY_CSV が未設定のためスキップ (データ非同梱のため既定でスキップ)。"));
		return true;
	}

	const FString FramesEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_FRAMES"));
	const int32 Frames = FramesEnv.IsEmpty() ? 60 : FCString::Atoi(*FramesEnv);
	const FString TolEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_TOL"));
	const float Tol = TolEnv.IsEmpty() ? 1e-3f : FCString::Atof(*TolEnv);

	// --- 基準 CSV ---
	FString CsvText;
	if (!FFileHelper::LoadFileToString(CsvText, *CsvPath))
	{
		AddError(FString::Printf(TEXT("基準 CSV を読めない: %s"), *CsvPath));
		return false;
	}
	TArray<FGoldenRow> Golden;
	FString ParseError;
	if (!ParseGolden(CsvText, Golden, ParseError))
	{
		AddError(FString::Printf(TEXT("基準 CSV の解析に失敗: %s"), *ParseError));
		return false;
	}

	// --- UE 側で同じことをする ---
	float UnitScale = 0.0f;
	TArray<FString> Warnings;
	TSharedPtr<PmxPhysicsModel> Model = GlbPhysicsReader::LoadFile(GlbPath, UnitScale, Warnings);
	for (const FString& W : Warnings) AddInfo(FString::Printf(TEXT("[warn] %s"), *W));
	if (!Model.IsValid())
	{
		AddError(FString::Printf(TEXT("GLB を読めない: %s"), *GlbPath));
		return false;
	}
	AddInfo(FString::Printf(TEXT("unitScale=%g bones=%d bodies=%d joints=%d"),
		UnitScale, Model->BoneNames.Num(), Model->RigidBodies.Num(), Model->Joints.Num()));

	TSharedPtr<PmxPhysicsBuilder> B = PmxPhysicsBuilder::Build(Model);
	AddInfo(FString::Printf(TEXT("built bodies=%d joints=%d pairs=%d"),
		B->Bodies.Num(), B->World.Joints.Num(), B->World.DebugCollisionPairCount()));

	if (!TestEqual(TEXT("剛体数が基準と一致する"), B->Bodies.Num(), Golden.Num()))
	{
		return false;
	}

	for (int32 f = 0; f < Frames; f++)
	{
		B->World.StepSimulation(1.0f / 30.0f);
	}

	// --- 突き合わせ ---
	float MaxPosErr = 0.0f, MaxRotErr = 0.0f;
	int32 WorstPos = -1, WorstRot = -1;
	int32 NonFinite = 0;
	FString Dump;
	Dump.Append(TEXT("index,name,px,py,pz,qx,qy,qz,qw\n"));

	for (int32 i = 0; i < B->Bodies.Num(); i++)
	{
		const RigidTransform& T = B->Bodies[i]->WorldTransform;
		Dump.Append(FString::Printf(TEXT("%d,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n"),
			i, *B->Bodies[i]->Name.Replace(TEXT(","), TEXT("_")),
			T.Origin.x, T.Origin.y, T.Origin.z, T.Rotation.x, T.Rotation.y, T.Rotation.z, T.Rotation.w));

		if (!FMath::IsFinite(T.Origin.x) || !FMath::IsFinite(T.Origin.y) || !FMath::IsFinite(T.Origin.z) ||
			!FMath::IsFinite(T.Rotation.w))
		{
			NonFinite++;
			continue;
		}

		const float PosErr = (T.Origin - Golden[i].Pos).Length();
		const float RotErr = QuatDelta(T.Rotation, Golden[i].Rot);
		if (PosErr > MaxPosErr) { MaxPosErr = PosErr; WorstPos = i; }
		if (RotErr > MaxRotErr) { MaxRotErr = RotErr; WorstRot = i; }
	}

	// UE 側の結果も残しておく (手で差分を見たいとき用)。
	const FString DumpPath = FPaths::ChangeExtension(CsvPath, TEXT("")) + TEXT("_ue.csv");
	FFileHelper::SaveStringToFile(Dump, *DumpPath);
	AddInfo(FString::Printf(TEXT("UE 側の結果を書き出した: %s"), *DumpPath));

	TestEqual(TEXT("NaN/Inf を出した剛体が無い"), NonFinite, 0);

	AddInfo(FString::Printf(TEXT("最大位置差 %.6g (剛体#%d %s)"),
		MaxPosErr, WorstPos, WorstPos >= 0 ? *B->Bodies[WorstPos]->Name : TEXT("-")));
	AddInfo(FString::Printf(TEXT("最大回転差 %.6g (剛体#%d %s)"),
		MaxRotErr, WorstRot, WorstRot >= 0 ? *B->Bodies[WorstRot]->Name : TEXT("-")));

	TestTrue(FString::Printf(TEXT("位置が C# 版と一致 (最大差 %.6g <= %.6g)"), MaxPosErr, Tol), MaxPosErr <= Tol);
	TestTrue(FString::Printf(TEXT("回転が C# 版と一致 (最大差 %.6g <= %.6g)"), MaxRotErr, Tol), MaxRotErr <= Tol);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
