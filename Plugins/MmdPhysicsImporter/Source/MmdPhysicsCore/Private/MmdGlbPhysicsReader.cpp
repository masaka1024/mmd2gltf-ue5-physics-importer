// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/GlbPhysicsReader.cs

#include "MmdGlbPhysicsReader.h"
#include "MmdPhysicsCoreLog.h"
#include "Misc/FileHelper.h"

namespace MmdPhysics
{
	namespace
	{
		FORCEINLINE uint32 ReadU32LE(const TArray<uint8>& d, int32 Offset)
		{
			return static_cast<uint32>(d[Offset])
				| (static_cast<uint32>(d[Offset + 1]) << 8)
				| (static_cast<uint32>(d[Offset + 2]) << 16)
				| (static_cast<uint32>(d[Offset + 3]) << 24);
		}

		FORCEINLINE float ReadF32LE(const TArray<uint8>& d, int32 Offset)
		{
			const uint32 Bits = ReadU32LE(d, Offset);
			float Out;
			FMemory::Memcpy(&Out, &Bits, sizeof(float));
			return Out;
		}

		/** MiniJson::Str は「文字列でない/欠損」を空文字で返すため、移植元の `?? fallback` を再現する。 */
		FString StrOr(const TSharedPtr<MmdJsonValue>& o, const FString& Fallback)
		{
			return (o.IsValid() && o->Type == EMmdJsonType::String) ? o->AsString : Fallback;
		}
	}

	TSharedPtr<PmxPhysicsModel> GlbPhysicsReader::LoadFile(const FString& Path)
	{
		float UnitScale = 0.0f;
		TArray<FString> Warnings;
		return LoadFile(Path, UnitScale, Warnings);
	}

	TSharedPtr<PmxPhysicsModel> GlbPhysicsReader::LoadFile(const FString& Path, float& OutUnitScale)
	{
		TArray<FString> Warnings;
		return LoadFile(Path, OutUnitScale, Warnings);
	}

	TSharedPtr<PmxPhysicsModel> GlbPhysicsReader::LoadFile(const FString& Path, float& OutUnitScale, TArray<FString>& OutWarnings)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			OutWarnings.Add(FString::Printf(TEXT("GLB を読めない: %s"), *Path));
			OutUnitScale = 1.0f;
			return nullptr;
		}
		return LoadBytes(Bytes, OutUnitScale, OutWarnings);
	}

	TSharedPtr<PmxPhysicsModel> GlbPhysicsReader::LoadBytes(const TArray<uint8>& Glb, float& OutUnitScale, TArray<FString>& OutWarnings)
	{
		TSharedPtr<MmdJsonValue> Root;
		TArray<uint8> Bin;
		if (!ParseGlb(Glb, Root, Bin, OutWarnings))
		{
			OutUnitScale = 1.0f;
			return nullptr;
		}
		return BuildModel(Root, OutUnitScale, OutWarnings);
	}

	TArray<Vec3> GlbPhysicsReader::BonePositionsFromIbm(const FString& Path)
	{
		TArray<Vec3> Result;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return Result;

		TSharedPtr<MmdJsonValue> Root;
		TArray<uint8> Bin;
		TArray<FString> Warnings;
		if (!ParseGlb(Bytes, Root, Bin, Warnings)) return Result;

		const FMmdJsonObject* mm = MiniJson::Obj(MiniJson::Get(MiniJson::Obj(MiniJson::Get(MiniJson::Obj(Root), TEXT("extras"))), TEXT("mmd")));
		const float s = MiniJson::Flt(MiniJson::Get(mm, TEXT("unitScale")));
		const FMmdJsonArray* skins = MiniJson::Arr(MiniJson::Get(MiniJson::Obj(Root), TEXT("skins")));
		if (skins == nullptr || skins->Num() == 0) return Result;
		const FMmdJsonObject* skin0 = MiniJson::Obj((*skins)[0]);
		const FMmdJsonArray* joints = MiniJson::Arr(MiniJson::Get(skin0, TEXT("joints")));
		if (joints == nullptr) return Result;
		const int32 nb = joints->Num();
		const int32 ibmAcc = MiniJson::Int(MiniJson::Get(skin0, TEXT("inverseBindMatrices")));
		const TArray<float> mats = ReadFloatAccessor(Root, Bin, ibmAcc); // nb*16
		if (mats.Num() < nb * 16) return Result;

		Result.SetNum(nb);
		for (int32 i = 0; i < nb; i++)
		{
			// 列優先 MAT4 の並進は要素 [12],[13],[14] = (-wp.x*s, -wp.y*s, wp.z*s)。
			const float tx = mats[i * 16 + 12], ty = mats[i * 16 + 13], tz = mats[i * 16 + 14];
			Result[i] = Vec3(-tx / s, -ty / s, tz / s);
		}
		return Result;
	}

	// ---- GLB コンテナ分解 (JSON チャンク + BIN チャンク) ----
	bool GlbPhysicsReader::ParseGlb(const TArray<uint8>& d, TSharedPtr<MmdJsonValue>& OutRoot, TArray<uint8>& OutBin, TArray<FString>& OutWarnings)
	{
		if (d.Num() < 12)
		{
			OutWarnings.Add(TEXT("GLB が短すぎる (ヘッダに満たない)"));
			return false;
		}
		const uint32 magic = ReadU32LE(d, 0);
		if (magic != 0x46546C67)
		{
			OutWarnings.Add(TEXT("GLB マジックが不正 (glTF ではない)"));
			return false;
		}
		// uint version = ReadU32LE(d, 4); uint length = ReadU32LE(d, 8);
		int32 off = 12;
		bool bHasJson = false;
		FString Json;
		OutBin.Reset();
		while (off + 8 <= d.Num())
		{
			const int32 clen = static_cast<int32>(ReadU32LE(d, off));
			const uint32 ctype = ReadU32LE(d, off + 4);
			const int32 cdata = off + 8;
			if (cdata + clen > d.Num()) break; // 壊れたチャンク長で範囲外を読まない
			if (ctype == 0x4E4F534A)      // "JSON"
			{
				Json = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(d.GetData() + cdata), clen));
				bHasJson = true;
			}
			else if (ctype == 0x004E4942) // "BIN\0"
			{
				OutBin.SetNumUninitialized(clen);
				FMemory::Memcpy(OutBin.GetData(), d.GetData() + cdata, clen);
			}
			off = cdata + clen;
			if ((clen & 3) != 0) off += 4 - (clen & 3); // 4バイト境界パディング
		}
		if (!bHasJson)
		{
			OutWarnings.Add(TEXT("GLB に JSON チャンクが無い"));
			return false;
		}
		OutRoot = MiniJson::Parse(Json);
		return true;
	}

	Vec3 GlbPhysicsReader::ComputeWorld(int32 i, int32 nb, const TArray<int32>& Parent, const TArray<Vec3>& LocalRaw,
		TArray<TOptional<Vec3>>& World)
	{
		if (World[i].IsSet()) return World[i].GetValue();
		const int32 p = Parent[i];
		const Vec3 w = (p >= 0 && p < nb) ? LocalRaw[i] + ComputeWorld(p, nb, Parent, LocalRaw, World) : LocalRaw[i];
		World[i] = w;
		return w;
	}

	// ---- extras.mmd → PmxPhysicsModel (防御的: 欠損/範囲外でも読み進める) ----
	TSharedPtr<PmxPhysicsModel> GlbPhysicsReader::BuildModel(const TSharedPtr<MmdJsonValue>& Root, float& OutUnitScale, TArray<FString>& OutWarnings)
	{
		TSharedPtr<PmxPhysicsModel> Model = MakeShared<PmxPhysicsModel>();
		const FMmdJsonObject* root = MiniJson::Obj(Root);
		const FMmdJsonObject* mm = MiniJson::Obj(MiniJson::Get(MiniJson::Obj(MiniJson::Get(root, TEXT("extras"))), TEXT("mmd")));
		FMmdJsonObject EmptyObj;
		bool bHadMmd = true;
		if (mm == nullptr)
		{
			OutWarnings.Add(TEXT("extras.mmd が無い (物理情報なし)。ボーンのみ読み進める。"));
			mm = &EmptyObj;
			bHadMmd = false;
		}
		float scale = MiniJson::Flt(MiniJson::Get(mm, TEXT("unitScale")));
		if (scale <= 0.0f)
		{
			if (bHadMmd && mm->Num() > 0) OutWarnings.Add(FString::Printf(TEXT("unitScale が不正 (%f)。1.0 として続行。"), scale));
			scale = 1.0f;
		}
		OutUnitScale = scale;

		// --- ボーン (nodes + skins[0].joints) ---
		const FMmdJsonArray* nodes = MiniJson::Arr(MiniJson::Get(root, TEXT("nodes")));
		const FMmdJsonArray* skins = MiniJson::Arr(MiniJson::Get(root, TEXT("skins")));
		const FMmdJsonObject* skin0 = (skins != nullptr && skins->Num() > 0) ? MiniJson::Obj((*skins)[0]) : nullptr;
		const FMmdJsonArray* jointNodes = MiniJson::Arr(MiniJson::Get(skin0, TEXT("joints")));
		const int32 nb = (jointNodes != nullptr && nodes != nullptr) ? jointNodes->Num() : 0;
		if (nodes == nullptr || skin0 == nullptr || jointNodes == nullptr)
			OutWarnings.Add(TEXT("nodes/skins/joints が無いためボーン0件で続行。"));

		TArray<Vec3> localRaw; localRaw.SetNum(nb);   // raw PMX ローカル並進
		TArray<int32> parent; parent.Init(-1, nb);

		for (int32 i = 0; i < nb; i++)
		{
			const int32 ni = MiniJson::Int((*jointNodes)[i]);
			const FMmdJsonObject* node = (ni >= 0 && ni < nodes->Num()) ? MiniJson::Obj((*nodes)[ni]) : nullptr;
			if (node == nullptr)
			{
				OutWarnings.Add(FString::Printf(TEXT("ボーン%d のノードindex %d が範囲外。"), i, ni));
				Model->BoneNames.Add(FString::Printf(TEXT("bone_%d"), i));
				Model->BoneDeformLayers.Add(0);
				continue;
			}
			Model->BoneNames.Add(StrOr(MiniJson::Get(node, TEXT("name")), FString::Printf(TEXT("bone_%d"), i)));
			const Vec3 lt = Vec3FromArr(MiniJson::Get(node, TEXT("translation")));
			localRaw[i] = Vec3(lt.x / scale, lt.y / scale, -lt.z / scale); // glTF→raw
			const FMmdJsonObject* ex = MiniJson::Obj(MiniJson::Get(MiniJson::Obj(MiniJson::Get(node, TEXT("extras"))), TEXT("mmd")));
			Model->BoneDeformLayers.Add(ex != nullptr ? MiniJson::Int(MiniJson::Get(ex, TEXT("layer"))) : 0);
		}
		if (nodes != nullptr)
		{
			// 親: 各ノードの children から。子ノードindexが 0..nb-1 のボーンなら親を設定。
			//     親ノードが 0..nb-1 のボーンなら親=そのindex、そうでなければ(スケルトンroot等)=-1。
			for (int32 j = 0; j < nodes->Num(); j++)
			{
				const FMmdJsonArray* ch = MiniJson::Arr(MiniJson::Get(MiniJson::Obj((*nodes)[j]), TEXT("children")));
				if (ch == nullptr) continue;
				for (const TSharedPtr<MmdJsonValue>& c : *ch)
				{
					const int32 ci = MiniJson::Int(c);
					if (ci >= 0 && ci < nb) parent[ci] = (j < nb) ? j : -1;
				}
			}
		}
		for (int32 i = 0; i < nb; i++) Model->BoneParents.Add(parent[i]);

		// world 位置 = ローカル並進の階層合成 (バインド回転恒等)。親を先に確定。
		TArray<TOptional<Vec3>> world; world.SetNum(nb);
		for (int32 i = 0; i < nb; i++) Model->BonePositions.Add(ComputeWorld(i, nb, parent, localRaw, world));

		// --- 剛体 (raw) ---
		const FMmdJsonArray* rbs = MiniJson::Arr(MiniJson::Get(mm, TEXT("rigidBodies")));
		if (rbs != nullptr)
		{
			for (const TSharedPtr<MmdJsonValue>& o : *rbs)
			{
				const FMmdJsonObject* r = MiniJson::Obj(o);
				PmxRigidBody Rb;
				Rb.Name = MiniJson::Str(MiniJson::Get(r, TEXT("name")));
				Rb.NameEn = MiniJson::Str(MiniJson::Get(r, TEXT("name_en")));
				Rb.BoneIndex = MiniJson::Int(MiniJson::Get(r, TEXT("bone")));
				Rb.Group = static_cast<uint8>(MiniJson::Int(MiniJson::Get(r, TEXT("group"))));
				Rb.NonCollisionGroup = static_cast<uint16>(MiniJson::Int(MiniJson::Get(r, TEXT("no_collision_mask"))));
				Rb.ShapeType = static_cast<uint8>(MiniJson::Int(MiniJson::Get(r, TEXT("shape"))));
				Rb.Size = Vec3FromArr(MiniJson::Get(r, TEXT("size")));
				Rb.Position = Vec3FromArr(MiniJson::Get(r, TEXT("pos")));
				Rb.Rotation = Vec3FromArr(MiniJson::Get(r, TEXT("rot")));
				Rb.Mass = MiniJson::Flt(MiniJson::Get(r, TEXT("mass")));
				Rb.LinearDamping = MiniJson::Flt(MiniJson::Get(r, TEXT("linear_damping")));
				Rb.AngularDamping = MiniJson::Flt(MiniJson::Get(r, TEXT("angular_damping")));
				Rb.Restitution = MiniJson::Flt(MiniJson::Get(r, TEXT("restitution")));
				Rb.Friction = MiniJson::Flt(MiniJson::Get(r, TEXT("friction")));
				Rb.PhysicsMode = static_cast<uint8>(MiniJson::Int(MiniJson::Get(r, TEXT("mode"))));
				Model->RigidBodies.Add(Rb);
			}
		}

		// --- Joint (raw) ---
		const FMmdJsonArray* jts = MiniJson::Arr(MiniJson::Get(mm, TEXT("joints")));
		if (jts != nullptr)
		{
			for (const TSharedPtr<MmdJsonValue>& o : *jts)
			{
				const FMmdJsonObject* j = MiniJson::Obj(o);
				PmxJoint Jt;
				Jt.Name = MiniJson::Str(MiniJson::Get(j, TEXT("name")));
				Jt.NameEn = MiniJson::Str(MiniJson::Get(j, TEXT("name_en")));
				Jt.JointType = static_cast<uint8>(MiniJson::Int(MiniJson::Get(j, TEXT("type"))));
				Jt.RigidBodyAIndex = MiniJson::Int(MiniJson::Get(j, TEXT("rigid_a")));
				Jt.RigidBodyBIndex = MiniJson::Int(MiniJson::Get(j, TEXT("rigid_b")));
				Jt.Position = Vec3FromArr(MiniJson::Get(j, TEXT("pos")));
				Jt.Rotation = Vec3FromArr(MiniJson::Get(j, TEXT("rot")));
				Jt.LinearLowerLimit = Vec3FromArr(MiniJson::Get(j, TEXT("pos_min")));
				Jt.LinearUpperLimit = Vec3FromArr(MiniJson::Get(j, TEXT("pos_max")));
				Jt.AngularLowerLimit = Vec3FromArr(MiniJson::Get(j, TEXT("rot_min")));
				Jt.AngularUpperLimit = Vec3FromArr(MiniJson::Get(j, TEXT("rot_max")));
				Jt.SpringLinear = Vec3FromArr(MiniJson::Get(j, TEXT("spring_pos")));
				Jt.SpringAngular = Vec3FromArr(MiniJson::Get(j, TEXT("spring_rot")));
				Model->Joints.Add(Jt);
			}
		}

		// --- 参照整合性の検査 (修正はせず警告のみ。PmxPhysicsBuilder 側が範囲外を許容する) ---
		for (int32 i = 0; i < Model->RigidBodies.Num(); i++)
		{
			if (Model->RigidBodies[i].BoneIndex >= Model->BoneNames.Num())
			{
				OutWarnings.Add(FString::Printf(TEXT("剛体 '%s' の関連ボーンindex %d が範囲外(ボーン数%d)。バインド位置扱い。"),
					*Model->RigidBodies[i].Name, Model->RigidBodies[i].BoneIndex, Model->BoneNames.Num()));
			}
		}
		for (int32 i = 0; i < Model->Joints.Num(); i++)
		{
			const PmxJoint& jj = Model->Joints[i]; const int32 rc = Model->RigidBodies.Num();
			if (jj.RigidBodyAIndex < 0 || jj.RigidBodyAIndex >= rc || jj.RigidBodyBIndex < 0 || jj.RigidBodyBIndex >= rc)
			{
				OutWarnings.Add(FString::Printf(TEXT("Joint '%s' が存在しない剛体を参照(A=%d B=%d, 剛体数%d)。構築時にスキップされる。"),
					*jj.Name, jj.RigidBodyAIndex, jj.RigidBodyBIndex, rc));
			}
		}

		return Model;
	}

	Vec3 GlbPhysicsReader::Vec3FromArr(const TSharedPtr<MmdJsonValue>& o)
	{
		const FMmdJsonArray* a = MiniJson::Arr(o);
		if (a == nullptr || a->Num() < 3) return Vec3::Zero;
		return Vec3(MiniJson::Flt((*a)[0]), MiniJson::Flt((*a)[1]), MiniJson::Flt((*a)[2]));
	}

	// accessor(FLOAT)を BIN から読み出す (bufferView.byteOffset + accessor.byteOffset)。
	TArray<float> GlbPhysicsReader::ReadFloatAccessor(const TSharedPtr<MmdJsonValue>& Root, const TArray<uint8>& Bin, int32 AccIdx)
	{
		TArray<float> outp;
		const FMmdJsonObject* root = MiniJson::Obj(Root);
		const FMmdJsonArray* accessors = MiniJson::Arr(MiniJson::Get(root, TEXT("accessors")));
		if (accessors == nullptr || !accessors->IsValidIndex(AccIdx)) return outp;
		const FMmdJsonObject* acc = MiniJson::Obj((*accessors)[AccIdx]);
		if (acc == nullptr) return outp;
		const int32 count = MiniJson::Int(MiniJson::Get(acc, TEXT("count")));
		const FString type = MiniJson::Str(MiniJson::Get(acc, TEXT("type")));
		const int32 comps = type == TEXT("MAT4") ? 16 : type == TEXT("VEC3") ? 3 : type == TEXT("VEC4") ? 4 : type == TEXT("SCALAR") ? 1 : 16;
		const int32 accOff = acc->Contains(TEXT("byteOffset")) ? MiniJson::Int(MiniJson::Get(acc, TEXT("byteOffset"))) : 0;
		const FMmdJsonArray* bufferViews = MiniJson::Arr(MiniJson::Get(root, TEXT("bufferViews")));
		const int32 bvIdx = MiniJson::Int(MiniJson::Get(acc, TEXT("bufferView")));
		if (bufferViews == nullptr || !bufferViews->IsValidIndex(bvIdx)) return outp;
		const FMmdJsonObject* bv = MiniJson::Obj((*bufferViews)[bvIdx]);
		if (bv == nullptr) return outp;
		const int32 bvOff = bv->Contains(TEXT("byteOffset")) ? MiniJson::Int(MiniJson::Get(bv, TEXT("byteOffset"))) : 0;
		const int32 baseOff = bvOff + accOff;
		outp.SetNumUninitialized(count * comps);
		for (int32 i = 0; i < outp.Num(); i++)
		{
			const int32 At = baseOff + i * 4;
			if (At + 4 > Bin.Num()) { outp.SetNum(i); break; }
			outp[i] = ReadF32LE(Bin, At);
		}
		return outp;
	}
}
