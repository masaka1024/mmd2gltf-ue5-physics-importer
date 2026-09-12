// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証A: 移植元 C# エンジンとの数値パリティ。
//
// 実モデルの GLB は再配布できないため、テストデータは環境変数で外から与える。
// 変数が無ければテストは何もせず成功する (データを持たない環境で赤くしないため)。
//
//   MMD_PARITY_GLB     … mmd2gltf-gui が出力した .glb (1 体)
//   MMD_PARITY_CSV     … Tools/CsReference が出力した基準 CSV (1 体)
//   MMD_PARITY_GLBS    … 複数モデル用。';' 区切り。**設定されていればこちらを優先**
//   MMD_PARITY_CSVS    … 同上。GLBS と**同数・同順**にすること
//
// ★複数モデルで回すこと。1 体だけだと形状・ジョイント型・質量域が偏ったまま緑になる。
//   複合テストなのでモデルごとに独立したケースとして出る:
//     MMD_PARITY_GLBS="A.glb;B.glb"  MMD_PARITY_CSVS="a.csv;b.csv"
//
// ★複数指定を MMD_PARITY_GLB に混ぜてはいけない。あれは ImportConvention / IdleSettle /
//   MaterialReader / ChainStability / ConvertMaterials / WirePhysics も**単一パスとして**
//   読んでいる共有の変数で、';' を入れると軒並み落ちる。だから末尾 S の別変数にしてある。
//   MMD_PARITY_FRAMES  … ステップ数 (既定 60。CSV を作ったときと同じ値にすること)
//   MMD_PARITY_TOL     … 位置の許容差 (PMX 単位。★既定 0 = ビット一致を要求する)
//
// ★許容差の既定は 0 である。このテストの目的は「C++ が C# とビット一致するか」であって
//   「近いか」ではない。1〜2 ULP の混入 (FMA 収縮・float 版の数学関数・列挙順序) は
//   接触が続く系で指数的に増幅し、60 フレームで 5.4e-01 まで開いた実測がある
//   (MmdPhysicsCore.Build.cs の FPSemantics の注記を参照)。ゆるい許容差はそれを見逃す。
//   環境差の調査などで一時的にゆるめたいときだけ MMD_PARITY_TOL を指定すること。
//
// 基準 CSV の作り方:
//   dotnet run --project Tools/CsReference -c Release -- <glb> 60 out/ia_60_cs.csv
//
// ★毎フレーム比較 (どのフレームで壊れたかを出す):
//   dotnet run --project Tools/CsReference -c Release -- <glb> 60 out/ia_60_cs_pf.csv --per-frame
//   で 1 行目が "frame," で始まる CSV を作ると、このテストは毎フレーム突き合わせて
//   **最初にずれたフレーム**を報告する。取り込みで壊れたときの切り分けが速くなる。
//
// ★アニメーションによる駆動は行わない。アニメを与えると取り込み経路の差まで混ざり、
//   物理エンジンの移植が正しいかを切り分けられなくなる。既定では両側とも
//   「AddBody がバインド姿勢で初期化した KinematicTarget のまま StepSimulation を回す」。
//
// ★駆動ありのパリティ (MMD_PARITY_DRIVE=1):
//   既定 (駆動なし) だけでは「毎フレーム kinematic ターゲットが更新される経路」を
//   一度も比較できない。駆動剛体が動いて鎖へ外力が入る経路・駆動剛体が揺れ物へ当たる経路も
//   同様に未比較のまま残る。そこで**式で決まる合成モーション**で駆動する (ApplyDrive)。
//   アニメではないので取り込み経路は混ざらず、駆動経路だけを比較できる。
//   基準 CSV も `--drive` を付けて作ること:
//     dotnet run --project Tools/CsReference -c Release -- <glb> 60 out/ia_60_drive_pf.csv --per-frame --drive

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

	/**
	 * 基準 CSV を読む。2 つの形式を自動判別する。
	 *   最終フレームのみ : index,name,px,py,pz,qx,qy,qz,qw
	 *   毎フレーム       : frame,index,name,px,py,pz,qx,qy,qz,qw  (1 行目が "frame," で始まる)
	 * 毎フレーム形式なら OutByFrame[f] に f+1 ステップ目の全剛体が入る。
	 */
	bool ParseGolden(const FString& CsvText, TArray<TArray<FGoldenRow>>& OutByFrame,
		bool& bOutPerFrame, FString& OutError)
	{
		TArray<FString> Lines;
		CsvText.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 2) { OutError = TEXT("CSV の行が足りない"); return false; }

		bOutPerFrame = Lines[0].StartsWith(TEXT("frame,"));
		// 毎フレーム形式は先頭に frame 列が入るぶん、各列が 1 つ後ろへずれる。
		const int32 Base = bOutPerFrame ? 1 : 0;
		const int32 MinCols = 9 + Base;

		OutByFrame.Reset();
		int32 CurFrame = -1;
		for (int32 L = 1; L < Lines.Num(); L++)
		{
			const FString& Line = Lines[L];
			if (Line.IsEmpty()) continue;
			TArray<FString> Cols;
			Line.ParseIntoArray(Cols, TEXT(","), false);
			if (Cols.Num() < MinCols)
			{
				OutError = FString::Printf(TEXT("%d 行目の列数が足りない (%d)"), L + 1, Cols.Num());
				return false;
			}
			if (bOutPerFrame)
			{
				const int32 Frame = FCString::Atoi(*Cols[0]);
				if (Frame != CurFrame) { OutByFrame.AddDefaulted(); CurFrame = Frame; }
			}
			else if (OutByFrame.Num() == 0)
			{
				OutByFrame.AddDefaulted();
			}

			FGoldenRow Row;
			Row.Name = Cols[Base + 1];
			Row.Pos = Vec3(FCString::Atof(*Cols[Base + 2]), FCString::Atof(*Cols[Base + 3]),
				FCString::Atof(*Cols[Base + 4]));
			Row.Rot = Quat(FCString::Atof(*Cols[Base + 5]), FCString::Atof(*Cols[Base + 6]),
				FCString::Atof(*Cols[Base + 7]), FCString::Atof(*Cols[Base + 8]));
			OutByFrame.Last().Add(Row);
		}
		if (OutByFrame.Num() == 0) { OutError = TEXT("データ行が無い"); return false; }
		return true;
	}

	// -----------------------------------------------------------------------
	// 駆動 (MMD_PARITY_DRIVE=1)
	//
	// ★アニメーションは使わない。アニメを与えると「取り込み経路の差」まで混ざり、
	//   物理エンジンの移植が正しいかを切り分けられなくなる (ヘッダの注記を参照)。
	//   そこで**式で決まる合成モーション**で駆動剛体を動かす。両側が同じ式・同じ定数・
	//   同じ数学関数 (MSin = C# の (float)Math.Sin) で計算するので、入力はビット単位で一致する。
	//
	// ★Tools/CsReference の ApplyDrive と**同じ式にすること**。片方だけ変えると
	//   パリティが落ちるが、原因が移植漏れなのか式の食い違いなのか分からなくなる。
	// -----------------------------------------------------------------------
	constexpr float KDriveSwayAmp = 1.0f;    // PMX 単位 (= 8cm)
	constexpr float KDriveRotAmp = 0.2f;     // ラジアン (約 11 度)
	constexpr float KDriveTwoPi = 6.2831853f;

	void ApplyDrive(PmxPhysicsBuilder& B, const PmxPhysicsModel& Model, int32 Frame)
	{
		const float t = Frame / 30.0f;
		const float Sway = KDriveSwayAmp * MSin(KDriveTwoPi * 0.7f * t);
		const float Ang = KDriveRotAmp * MSin(KDriveTwoPi * 0.5f * t);
		const float Half = Ang * 0.5f;
		const Quat Q(0.0f, MSin(Half), 0.0f, MCos(Half));

		B.ApplyKinematicTargets([&Model, &Q, Sway](int32 i) -> TOptional<RigidTransform>
		{
			if (i < 0 || i >= Model.BonePositions.Num()) return TOptional<RigidTransform>();
			const Vec3& P = Model.BonePositions[i];
			return TOptional<RigidTransform>(RigidTransform(Q, Vec3(P.x + Sway, P.y, P.z)));
		});
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

// ★複合テストにしてある。MMD_PARITY_GLB / MMD_PARITY_CSV を ';' 区切りで複数渡すと
//   **モデルごとに独立したテストケース**として走り、どのモデルで落ちたかがそのまま出る。
//   1 体しか渡さなければ従来と同じ (区切りが無ければ要素 1 のリストになる)。
//   モデルを 1 体しか見ていないと、形状・ジョイント型・質量域が偏ったまま緑になる。
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMmdPhysicsGlbParityTest, "MmdPhysics.Core.GlbParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMmdPhysicsGlbParityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	// ★複数指定は専用の変数 (末尾 S) で受ける。
	//   MMD_PARITY_GLB は ImportConvention / IdleSettle / MaterialReader / ChainStability /
	//   ConvertMaterials / WirePhysics も**単一パスとして**読んでいる共有の変数なので、
	//   ここを ';' 区切りにすると他のテストが壊れる (実際に IdleSettle と MaterialReader が落ちた)。
	FString GlbEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLBS"));
	FString CsvEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_CSVS"));
	if (GlbEnv.IsEmpty() || CsvEnv.IsEmpty())
	{
		GlbEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
		CsvEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_CSV"));
	}

	TArray<FString> Glbs, Csvs;
	GlbEnv.ParseIntoArray(Glbs, TEXT(";"), true);
	CsvEnv.ParseIntoArray(Csvs, TEXT(";"), true);

	// 未設定・数が合わないときも 1 件は出す。RunTest 側がスキップかエラーを報告する
	// (ここで 0 件にすると「テストが存在しない」扱いになり、取りこぼしに気付けない)。
	if (Glbs.Num() == 0 || Glbs.Num() != Csvs.Num())
	{
		OutBeautifiedNames.Add(TEXT("Default"));
		OutTestCommands.Add(FString());
		return;
	}

	for (int32 i = 0; i < Glbs.Num(); i++)
	{
		// ★'.' はオートメーションの階層区切りなので置き換える。
		//   "Tda式初音ミク・アペンド_Ver1.10" をそのまま渡すと "..._Ver1" と "10" に割れて、
		//   テスト名が "10" として出る (実際にそうなった)。
		OutBeautifiedNames.Add(FPaths::GetBaseFilename(Glbs[i]).Replace(TEXT("."), TEXT("_")));
		OutTestCommands.Add(Glbs[i] + TEXT("|") + Csvs[i]);
	}
}

bool FMmdPhysicsGlbParityTest::RunTest(const FString& Parameters)
{
	FString GlbPath, CsvPath;
	if (!Parameters.Split(TEXT("|"), &GlbPath, &CsvPath))
	{
		// GetTests が組を作れなかったとき。未設定ならスキップ、数違いならエラー。
		const bool bPlural = !FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLBS")).IsEmpty()
			|| !FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_CSVS")).IsEmpty();
		if (!bPlural && (FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB")).IsEmpty()
			|| FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_CSV")).IsEmpty()))
		{
			AddInfo(TEXT("MMD_PARITY_GLB / MMD_PARITY_CSV が未設定のためスキップ (データ非同梱のため既定でスキップ)。"));
			return true;
		}
		AddError(TEXT("GLB と CSV の件数が合いません (';' 区切りで同数にすること)。"));
		return false;
	}
	AddInfo(FString::Printf(TEXT("モデル: %s"), *FPaths::GetCleanFilename(GlbPath)));

	const FString FramesEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_FRAMES"));
	const int32 Frames = FramesEnv.IsEmpty() ? 60 : FCString::Atoi(*FramesEnv);
	const FString TolEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_TOL"));
	// ★既定は 0 (ビット一致を要求)。ヘッダの注記を参照。
	const float Tol = TolEnv.IsEmpty() ? 0.0f : FCString::Atof(*TolEnv);
	// 駆動あり。基準 CSV も --drive で作ったものを渡すこと (食い違うと当然落ちる)。
	const bool bDrive = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_DRIVE")) == TEXT("1");

	// --- 基準 CSV ---
	FString CsvText;
	if (!FFileHelper::LoadFileToString(CsvText, *CsvPath))
	{
		AddError(FString::Printf(TEXT("基準 CSV を読めない: %s"), *CsvPath));
		return false;
	}
	TArray<TArray<FGoldenRow>> GoldenByFrame;
	bool bPerFrame = false;
	FString ParseError;
	if (!ParseGolden(CsvText, GoldenByFrame, bPerFrame, ParseError))
	{
		AddError(FString::Printf(TEXT("基準 CSV の解析に失敗: %s"), *ParseError));
		return false;
	}
	// 最終フレームの基準。以降の突き合わせはこれを使う (従来と同じ)。
	const TArray<FGoldenRow>& Golden = GoldenByFrame.Last();
	if (bPerFrame)
	{
		AddInfo(FString::Printf(TEXT("毎フレーム形式の基準 CSV (%d フレーム分)。最初にずれたフレームを報告する。"),
			GoldenByFrame.Num()));
		if (GoldenByFrame.Num() != Frames)
		{
			AddWarning(FString::Printf(
				TEXT("基準 CSV は %d フレーム分だが MMD_PARITY_FRAMES=%d。重なる範囲だけ比較する。"),
				GoldenByFrame.Num(), Frames));
		}
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

	// ★毎フレーム形式なら 1 ステップごとに突き合わせ、最初にずれたフレームを覚える。
	//   最終フレームだけ見ていると「いつ壊れたか」が落ちてしまい、取り込みで壊れたときの
	//   切り分けに使えない。ずれを見つけても最後まで回す (最終差も併せて報告するため)。
	int32 FirstBadFrame = -1;
	int32 FirstBadBody = -1;
	float FirstBadPos = 0.0f, FirstBadRot = 0.0f;

	for (int32 f = 0; f < Frames; f++)
	{
		if (bDrive) ApplyDrive(*B, *Model, f + 1);
		B->World.StepSimulation(1.0f / 30.0f);

		if (!bPerFrame || FirstBadFrame >= 0 || !GoldenByFrame.IsValidIndex(f)) continue;

		const TArray<FGoldenRow>& G = GoldenByFrame[f];
		if (G.Num() != B->Bodies.Num()) continue;   // 行数が合わない分は最終判定に任せる
		for (int32 i = 0; i < B->Bodies.Num(); i++)
		{
			const RigidTransform& T = B->Bodies[i]->WorldTransform;
			const float P = (T.Origin - G[i].Pos).Length();
			const float R = QuatDelta(T.Rotation, G[i].Rot);
			if (P > Tol || R > Tol)
			{
				FirstBadFrame = f + 1;   // 1 始まり (CSV の frame 列と揃える)
				FirstBadBody = i;
				FirstBadPos = P;
				FirstBadRot = R;
				break;
			}
		}
	}

	if (bPerFrame)
	{
		if (FirstBadFrame < 0)
		{
			AddInfo(FString::Printf(TEXT("全 %d フレームで基準と一致 (許容差 %.6g)。"),
				FMath::Min(Frames, GoldenByFrame.Num()), Tol));
		}
		else
		{
			AddError(FString::Printf(
				TEXT("★最初にずれたフレーム: %d (剛体#%d %s / 位置差 %.6g / 回転差 %.6g、許容差 %.6g)"),
				FirstBadFrame, FirstBadBody,
				B->Bodies.IsValidIndex(FirstBadBody) ? *B->Bodies[FirstBadBody]->Name : TEXT("-"),
				FirstBadPos, FirstBadRot, Tol));
		}
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
