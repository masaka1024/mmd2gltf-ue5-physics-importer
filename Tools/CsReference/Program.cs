// Copyright (c) 2026 masaka1024. MIT License.
//
// 検証A の基準出力を作る。移植元 C# エンジンで GLB を読み、N フレーム回して
// 全剛体のワールド姿勢を CSV へ落とす。UE 側の MmdPhysics.Core.GlbParity が
// 同じ入力・同じ手順で回した結果とこれを突き合わせる。
//
// ★ボーンアニメーションによる駆動は一切行わない。
//   アニメを与えると「アニメの取り込み経路」の差まで混ざり、
//   物理エンジンの移植が正しいかどうかを切り分けられなくなる。
//   PhysicsWorld.AddBody が kinematic 剛体の KinematicTarget をバインド姿勢で
//   初期化するので、駆動なしでも「体は静止・揺れ物は重力で落ちる」状態を再現できる。
//
// ★ただし駆動なしだけでは「毎フレーム kinematic ターゲットが更新される経路」を
//   一度も比較できない。そこで --drive を足した。アニメではなく**式で決まる合成モーション**
//   なので、取り込み経路は混ざらないまま駆動経路だけを比較できる (ApplyDrive を参照)。
//
// 使い方:
//   dotnet run --project Tools/CsReference -- <glb> <frames> <out.csv> [--per-frame] [--drive] [--playback]
//
// --per-frame を付けると、最終フレームだけでなく **毎フレーム** の全剛体姿勢を出す
// (ヘッダが "frame," で始まる)。UE 側の GlbParity はこの形式を自動判別して毎フレーム
// 突き合わせ、**最初にずれたフレーム**を報告する。取り込みで壊れたときに
// 「どのフレームから壊れたか」が分かると切り分けが速い。
// 既定 (フラグ無し) は従来どおり最終フレームのみで、既存の基準 CSV と同じ形式。

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using BulletPhysics;
using BulletPhysics.Pmx;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("usage: MmdCsReference <glb> <frames> <out.csv>");
            return 2;
        }

        string glbPath = args[0];
        int frames = int.Parse(args[1], CultureInfo.InvariantCulture);
        string outPath = args[2];
        bool perFrame = Array.IndexOf(args, "--per-frame") >= 0;
        bool drive = Array.IndexOf(args, "--drive") >= 0;
        bool playback = Array.IndexOf(args, "--playback") >= 0;

        var model = GlbPhysicsReader.LoadFile(glbPath, out float unitScale, out List<string> warnings);
        foreach (var w in warnings) Console.Error.WriteLine("[warn] " + w);
        Console.Error.WriteLine($"unitScale={unitScale.ToString("R", CultureInfo.InvariantCulture)} " +
                                $"bones={model.BoneNames.Count} bodies={model.RigidBodies.Count} joints={model.Joints.Count}");

        var builder = PmxPhysicsBuilder.Build(model);
        Console.Error.WriteLine($"built bodies={builder.Bodies.Count} joints={builder.World.Joints.Count} " +
                                $"pairs={builder.World.DebugCollisionPairCount}");

        if (playback) ApplyPlaybackSettings(builder.World);

        var sb = new StringBuilder();
        sb.Append(perFrame ? "frame,index,name,px,py,pz,qx,qy,qz,qw\n"
                           : "index,name,px,py,pz,qx,qy,qz,qw\n");

        // 1 行分を書く。perFrame のときだけ先頭に frame 列が付く。
        void Emit(int frame, int i)
        {
            var b = builder.Bodies[i];
            var t = b.WorldTransform;
            if (perFrame) sb.Append(frame.ToString(CultureInfo.InvariantCulture)).Append(',');
            sb.Append(i.ToString(CultureInfo.InvariantCulture)).Append(',');
            sb.Append(b.Name.Replace(',', '_')).Append(',');
            sb.Append(F(t.Origin.x)).Append(',').Append(F(t.Origin.y)).Append(',').Append(F(t.Origin.z)).Append(',');
            sb.Append(F(t.Rotation.x)).Append(',').Append(F(t.Rotation.y)).Append(',')
              .Append(F(t.Rotation.z)).Append(',').Append(F(t.Rotation.w)).Append('\n');
        }

        for (int f = 0; f < frames; f++)
        {
            if (drive) ApplyDrive(builder, model, f + 1);
            builder.World.StepSimulation(1f / 30f);
            // frame 列は 1 始まり (「何ステップ回した後か」と一致させる)。
            if (perFrame)
                for (int i = 0; i < builder.Bodies.Count; i++) Emit(f + 1, i);
        }

        if (!perFrame)
            for (int i = 0; i < builder.Bodies.Count; i++) Emit(frames, i);

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath)));
        File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
        Console.Error.WriteLine(perFrame
            ? $"wrote {builder.Bodies.Count} bodies x {frames} frames -> {outPath}"
            : $"wrote {builder.Bodies.Count} rows -> {outPath}");
        return 0;
    }

    // -----------------------------------------------------------------------
    // 再生時のソルバ設定 (--playback)
    //
    // ★コア既定と再生時の設定は大きく違う。パリティはこれまでコア既定でしか比べておらず、
    //   **実際に再生されるときの構成は一度も比較されていなかった**。
    //   値は UE 側 FAnimNode_MmdPhysics の既定 (= 再生時に使われる値) に合わせてある。
    //
    //              コア既定          再生時 (ノード既定)
    //   SolverIterations        10        10
    //   JointVelocityIterations  0        40
    //   JointMaxCorrectionVel    0        30
    //   SubSteps                 4         2
    //   FixedTimeStep         1/30      1/60
    //   UseSplitImpulse      false      true
    //   UseJointSplitImpulse false      true
    //
    // ★UE 側の ApplyPlaybackSettings と**同じ値にすること**。片方だけ変えるとパリティは
    //   落ちるが、原因が移植漏れなのか設定の食い違いなのか分からなくなる。
    // -----------------------------------------------------------------------
    private static void ApplyPlaybackSettings(PhysicsWorld w)
    {
        w.SolverIterations = 10;
        w.JointVelocityIterations = 40;
        w.JointMaxCorrectionVel = 30f;
        w.SubSteps = 2;
        w.FixedTimeStep = 1f / 60f;
        w.UseSplitImpulse = true;
        w.UseJointSplitImpulse = true;
    }

    // -----------------------------------------------------------------------
    // 駆動 (--drive)
    //
    // ★アニメーションは使わない。ファイルの冒頭に書いたとおり、アニメを与えると
    //   「取り込み経路の差」まで混ざって物理の移植を切り分けられなくなる。
    //   そこで**式で決まる合成モーション**で駆動剛体を動かす。両側が同じ式・同じ定数・
    //   同じ数学関数 ((float)Math.Sin = UE 側の MSin) で計算するので、入力はビット単位で一致する。
    //
    // 中身: 駆動ボーンをバインド位置から X 方向へ揺らし、姿勢に Y 軸回転を与える。
    //   これで「毎フレーム更新される kinematic ターゲット」「駆動剛体が動いて鎖へ外力が入る」
    //   「駆動剛体が揺れ物へ当たる」という、従来の駆動なしパリティが一度も通らなかった経路を通す。
    //   物理的な自然さは狙っていない (パリティの目的は挙動の一致であって見た目ではない)。
    // -----------------------------------------------------------------------
    private const float DriveSwayAmp = 1.0f;    // PMX 単位 (= 8cm)
    private const float DriveRotAmp = 0.2f;     // ラジアン (約 11 度)
    private const float DriveTwoPi = 6.2831853f;

    private static void ApplyDrive(PmxPhysicsBuilder builder, PmxPhysicsModel model, int frame)
    {
        float t = frame / 30f;
        float sway = DriveSwayAmp * (float)Math.Sin(DriveTwoPi * 0.7f * t);
        float ang = DriveRotAmp * (float)Math.Sin(DriveTwoPi * 0.5f * t);
        float half = ang * 0.5f;
        var q = new Quat(0f, (float)Math.Sin(half), 0f, (float)Math.Cos(half));

        builder.ApplyKinematicTargets(i =>
        {
            if (i < 0 || i >= model.BonePositions.Count) return null;
            var p = model.BonePositions[i];
            return new RigidTransform(q, new Vec3(p.x + sway, p.y, p.z));
        });
    }

    // "R" は float の往復可能な最短表現。UE 側は %.9g で出すので、比較は数値で行う。
    private static string F(float v) => v.ToString("R", CultureInfo.InvariantCulture);
}
