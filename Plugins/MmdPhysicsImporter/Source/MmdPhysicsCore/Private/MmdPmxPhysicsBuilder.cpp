// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Pmx/PmxPhysicsBuilder.cs

#include "MmdPmxPhysicsBuilder.h"

namespace MmdPhysics
{
	TSharedPtr<PmxPhysicsBuilder> PmxPhysicsBuilder::Build(const TSharedPtr<PmxPhysicsModel>& Model)
	{
		TSharedPtr<PmxPhysicsBuilder> b = MakeShared<PmxPhysicsBuilder>();
		b->_model = Model;
		b->BuildBodies(*Model);
		b->BuildJoints(*Model);
		return b;
	}

	void PmxPhysicsBuilder::BuildBodies(const PmxPhysicsModel& Model)
	{
		for (const PmxRigidBody& rb : Model.RigidBodies)
		{
			TSharedPtr<CollisionShape> shape = CreateShape(rb);
			TSharedPtr<RigidBody> body = MakeShared<RigidBody>(shape);
			body->Name = rb.Name;
			body->BoneIndex = rb.BoneIndex;
			body->Group = rb.Group;
			// PMX の 16bit フィールドは bit=1 が「そのグループと衝突する」を意味するので
			// そのまま衝突マスクとして渡す (Bullet の collision mask 相当)。
			body->CollisionMask = rb.NonCollisionGroup;
			body->Mode = static_cast<EPhysicsMode>(rb.PhysicsMode);
			body->LinearDamping = rb.LinearDamping;
			body->AngularDamping = rb.AngularDamping;
			body->Restitution = rb.Restitution;
			body->Friction = rb.Friction;
			body->WorldTransform = RigidTransform::FromEuler(rb.Position, rb.Rotation);

			body->KinematicTarget = body->WorldTransform;
			// ボーン追従は質量 0 (kinematic)、それ以外は PMX 質量。
			body->SetMassProps(body->Mode == EPhysicsMode::BoneFollow ? 0.0f : rb.Mass);

			World.AddBody(body);
			Bodies.Add(body.Get());

			// ボーンオフセット。
			BoneLink link;
			link.Body = body.Get();
			link.BoneIndex = rb.BoneIndex;
			link.Mode = body->Mode;
			link.BodyOffsetFromBone = ComputeOffset(Model, rb);
			BoneLinks.Add(link);
			if (link.Mode == EPhysicsMode::DynamicBoneMerge) _hasBoneMerge = true;
		}
	}

	void PmxPhysicsBuilder::ResetBodiesToBonePose(FBoneWorldGetter GetBoneWorld)
	{
		for (const BoneLink& link : BoneLinks)
		{
			RigidBody* body = link.Body;
			if (link.BoneIndex >= 0)
			{
				const TOptional<RigidTransform> bw = GetBoneWorld(link.BoneIndex);
				if (bw.IsSet())
				{
					body->WorldTransform = bw.GetValue() * link.BodyOffsetFromBone;
					body->KinematicTarget = body->WorldTransform;
					body->KinematicStepTarget = body->WorldTransform;
				}
			}
			body->LinearVelocity = Vec3::Zero;
			body->AngularVelocity = Vec3::Zero;
			body->UpdateInertiaWorld();
		}
		World.ClearContacts();
	}

	RigidTransform PmxPhysicsBuilder::Fk(int32 i, int32 Depth, int32 n, const TArray<bool>& IsPhysics,
		TArray<TOptional<RigidTransform>>& World_, FBoneWorldGetter GetDrivenBoneWorld) const
	{
		if (World_[i].IsSet()) return World_[i].GetValue();
		// 循環参照の保険 (深さ上限で打ち切りバインド位置)。
		if (Depth > 512) { World_[i] = RigidTransform(Quat::Identity, _model->BonePositions[i]); return World_[i].GetValue(); }

		// 物理ボーンは駆動姿勢を使わず必ず FK。それ以外は駆動姿勢があれば使う。
		const TOptional<RigidTransform> driven = IsPhysics[i] ? TOptional<RigidTransform>() : GetDrivenBoneWorld(i);
		RigidTransform res;
		if (driven.IsSet()) res = driven.GetValue();
		else
		{
			const int32 p = (i < _model->BoneParents.Num()) ? _model->BoneParents[i] : -1;
			if (p < 0 || p >= n)
				res = RigidTransform(Quat::Identity, _model->BonePositions[i]); // ルート等 = バインド世界
			else
			{
				const RigidTransform pw = Fk(p, Depth + 1, n, IsPhysics, World_, GetDrivenBoneWorld);
				const Vec3 localOff = _model->BonePositions[i] - _model->BonePositions[p]; // バインドは回転恒等
				res = RigidTransform(pw.Rotation, pw.Rotation * localOff + pw.Origin);
			}
		}
		World_[i] = res;
		return res;
	}

	void PmxPhysicsBuilder::ResetBodiesToBonePoseFk(FBoneWorldGetter GetDrivenBoneWorld)
	{
		const int32 n = _model->BoneNames.Num();

		// 物理ボーン = 非 BoneFollow (動的/物理+Bone合わせ) 剛体が紐づくボーン。
		TArray<bool> isPhysics;
		isPhysics.SetNumZeroed(n);
		for (const BoneLink& link : BoneLinks)
		{
			if (link.BoneIndex >= 0 && link.BoneIndex < n && link.Mode != EPhysicsMode::BoneFollow)
				isPhysics[link.BoneIndex] = true;
		}

		TArray<TOptional<RigidTransform>> world;
		world.SetNum(n);
		for (int32 i = 0; i < n; i++) if (!world[i].IsSet()) Fk(i, 0, n, isPhysics, world, GetDrivenBoneWorld);

		// 計算した FK-rest 姿勢で配置 (原始 API へ委譲)。
		ResetBodiesToBonePose([&world, n](int32 i) -> TOptional<RigidTransform>
		{
			return (i >= 0 && i < n) ? world[i] : TOptional<RigidTransform>();
		});
	}

	void PmxPhysicsBuilder::ApplyKinematicTargets(FBoneWorldGetter GetDrivenBoneWorld)
	{
		for (const BoneLink& link : BoneLinks)
		{
			if (link.Mode != EPhysicsMode::BoneFollow || link.BoneIndex < 0) continue;
			const TOptional<RigidTransform> bw = GetDrivenBoneWorld(link.BoneIndex);
			if (bw.IsSet()) link.Body->KinematicTarget = bw.GetValue() * link.BodyOffsetFromBone;
		}
	}

	Quat PmxPhysicsBuilder::EulerQ(const Vec3& e)
	{
		const float sx = MSin(e.x * 0.5f), cx = MCos(e.x * 0.5f);
		const float sy = MSin(e.y * 0.5f), cy = MCos(e.y * 0.5f);
		const float sz = MSin(e.z * 0.5f), cz = MCos(e.z * 0.5f);
		return Quat(sx, 0, 0, cx) * Quat(0, sy, 0, cy) * Quat(0, 0, sz, cz);
	}

	RigidTransform PmxPhysicsBuilder::Align(int32 i, int32 Depth, int32 n,
		const TArray<TOptional<Quat>>& PhysRot,
		const TMap<RigidBody*, BoneLink>& LinkOf,
		const TMap<int32, Joint*>* ParentJoint,
		float RotClampAlpha,
		TArray<TOptional<RigidTransform>>& World_,
		FBoneWorldGetter GetDrivenBoneWorld) const
	{
		if (World_[i].IsSet()) return World_[i].GetValue();
		if (Depth > 512) { World_[i] = RigidTransform(Quat::Identity, _model->BonePositions[i]); return World_[i].GetValue(); }
		RigidTransform res;
		const int32 p = (i < _model->BoneParents.Num()) ? _model->BoneParents[i] : -1;
		if (!PhysRot[i].IsSet())
		{
			// 非物理: 駆動姿勢があればそれ、無ければ FK (バインドは回転恒等)。
			const TOptional<RigidTransform> driven = GetDrivenBoneWorld(i);
			if (driven.IsSet()) res = driven.GetValue();
			else if (p < 0 || p >= n) res = RigidTransform(Quat::Identity, _model->BonePositions[i]);
			else
			{
				const RigidTransform pw = Align(p, Depth + 1, n, PhysRot, LinkOf, ParentJoint, RotClampAlpha, World_, GetDrivenBoneWorld);
				res = RigidTransform(pw.Rotation, pw.Rotation * (_model->BonePositions[i] - _model->BonePositions[p]) + pw.Origin);
			}
		}
		else
		{
			// 物理: 回転 = 物理 (RotClampAlpha>0 なら親側ジョイントのリミットへ α で戻す)。
			//        位置 = 親(補正済)から再構成。親無しはバインド位置。
			Quat rot = PhysRot[i].GetValue();
			Joint* const* pjFound = (ParentJoint != nullptr) ? ParentJoint->Find(i) : nullptr;
			if (pjFound != nullptr)
			{
				Joint* pj = *pjFound;
				// 補正済み親フレーム基準の相対euler (エンジンと同じ分解 Joint::ToEulerXYZ)。
				const BoneLink& aLink = LinkOf[pj->BodyA];
				RigidTransform aBonePose;
				if (aLink.BoneIndex >= 0 && aLink.BoneIndex < n)
					aBonePose = Align(aLink.BoneIndex, Depth + 1, n, PhysRot, LinkOf, ParentJoint, RotClampAlpha, World_, GetDrivenBoneWorld);
				else
					aBonePose = pj->BodyA->WorldTransform * aLink.BodyOffsetFromBone.Inverse();
				const Quat qA = ((aBonePose * aLink.BodyOffsetFromBone).Rotation * pj->FrameInA.Rotation).Normalized();
				const BoneLink& bLink = LinkOf[pj->BodyB];
				const Quat qB = ((rot * bLink.BodyOffsetFromBone.Rotation) * pj->FrameInB.Rotation).Normalized();
				const Vec3 eu = Joint::ToEulerXYZ((qA.Conjugated() * qB).Normalized());
				Vec3 ec = eu; bool changed = false;
				for (int32 d = 0; d < 3; d++)
				{
					const float lo = pj->AngularLowerLimit[d], hi = pj->AngularUpperLimit[d];
					if (lo > hi) continue; // free
					if (ec[d] < lo) { ec[d] += RotClampAlpha * (lo - ec[d]); changed = true; }
					else if (ec[d] > hi) { ec[d] += RotClampAlpha * (hi - ec[d]); changed = true; }
				}
				if (changed)
				{
					const Quat qBnew = qA * EulerQ(ec);
					rot = (qBnew * pj->FrameInB.Rotation.Conjugated()) * bLink.BodyOffsetFromBone.Rotation.Conjugated();
				}
			}
			if (p < 0 || p >= n) res = RigidTransform(rot, _model->BonePositions[i]);
			else
			{
				const RigidTransform pw = Align(p, Depth + 1, n, PhysRot, LinkOf, ParentJoint, RotClampAlpha, World_, GetDrivenBoneWorld);
				res = RigidTransform(rot, pw.Rotation * (_model->BonePositions[i] - _model->BonePositions[p]) + pw.Origin);
			}
		}
		World_[i] = res;
		return res;
	}

	TArray<TOptional<RigidTransform>> PmxPhysicsBuilder::ComputeAlignedBonePoses(
		FBoneWorldGetter GetDrivenBoneWorld, float RotClampAlpha)
	{
		const int32 n = _model->BoneNames.Num();
		TArray<TOptional<Quat>> physRot; physRot.SetNum(n); // 物理ボーンの復元回転 (位置は捨てる)
		TMap<RigidBody*, BoneLink> linkOf;
		for (const BoneLink& link : BoneLinks)
		{
			if (link.Body != nullptr) linkOf.Add(link.Body, link);
			if (link.BoneIndex >= 0 && link.BoneIndex < n && link.Mode != EPhysicsMode::BoneFollow)
				physRot[link.BoneIndex] = (link.Body->WorldTransform * link.BodyOffsetFromBone.Inverse()).Rotation;
		}
		// 物理ボーン -> 親側ジョイント (BodyB=自分の剛体)。clamp 用。
		TMap<int32, Joint*> parentJointMap;
		const TMap<int32, Joint*>* parentJoint = nullptr;
		if (RotClampAlpha > 0.0f)
		{
			// 階層親(BoneParents)と一致するジョイントを優先 (ring0=取付, ring1/2=縦, 髪=鎖)。
			// 横ジョイント(隣接同士)を誤って親に選ぶと clamp が的を外す (α=1でも取付超過が残った実測の原因)。
			for (const TSharedPtr<Joint>& jp : World.Joints)
			{
				Joint* j = jp.Get();
				if (j->BodyB != nullptr && j->BodyA != nullptr && linkOf.Contains(j->BodyB) && linkOf.Contains(j->BodyA))
				{
					const int32 bi = linkOf[j->BodyB].BoneIndex;
					if (bi < 0 || bi >= n || !physRot[bi].IsSet()) continue;
					const int32 hierParent = (bi < _model->BoneParents.Num()) ? _model->BoneParents[bi] : -1;
					const bool isHier = linkOf[j->BodyA].BoneIndex == hierParent;
					if (!parentJointMap.Contains(bi)) parentJointMap.Add(bi, j);
					else if (isHier && linkOf[parentJointMap[bi]->BodyA].BoneIndex != hierParent) parentJointMap[bi] = j;
				}
			}
			parentJoint = &parentJointMap;
		}

		TArray<TOptional<RigidTransform>> world;
		world.SetNum(n);
		for (int32 i = 0; i < n; i++)
		{
			if (!world[i].IsSet()) Align(i, 0, n, physRot, linkOf, parentJoint, RotClampAlpha, world, GetDrivenBoneWorld);
		}
		return world;
	}

	RigidTransform PmxPhysicsBuilder::ComputeOffset(const PmxPhysicsModel& Model, const PmxRigidBody& rb)
	{
		const RigidTransform bodyWorld = RigidTransform::FromEuler(rb.Position, rb.Rotation);
		if (rb.BoneIndex < 0 || rb.BoneIndex >= Model.BonePositions.Num())
			return bodyWorld; // ボーン無し
		// バインド時ボーンは回転恒等・位置のみ。
		const RigidTransform boneWorld(Quat::Identity, Model.BonePositions[rb.BoneIndex]);
		return boneWorld.InverseTimes(bodyWorld);
	}

	TSharedPtr<CollisionShape> PmxPhysicsBuilder::CreateShape(const PmxRigidBody& rb)
	{
		switch (rb.ShapeType)
		{
		case 0: return MakeShared<SphereShape>(rb.Size.x);
		case 1: return MakeShared<BoxShape>(rb.Size);
		case 2: return MakeShared<CapsuleShape>(rb.Size.x, rb.Size.y);
		default: return MakeShared<SphereShape>(rb.Size.x);
		}
	}

	void PmxPhysicsBuilder::BuildJoints(const PmxPhysicsModel& Model)
	{
		for (const PmxJoint& pj : Model.Joints)
		{
			RigidBody* a = ValidBody(pj.RigidBodyAIndex);
			RigidBody* b = ValidBody(pj.RigidBodyBIndex);
			if (a == nullptr || b == nullptr) continue; // 両端必須

			const RigidTransform worldFrame = RigidTransform::FromEuler(pj.Position, pj.Rotation);
			TSharedPtr<Joint> joint = Joint::FromPmx(
				static_cast<EJointType>(pj.JointType), a, b, worldFrame,
				pj.LinearLowerLimit, pj.LinearUpperLimit,
				pj.AngularLowerLimit, pj.AngularUpperLimit,
				pj.SpringLinear, pj.SpringAngular);
			joint->Name = pj.Name;
			World.AddJoint(joint);
		}
	}
}
