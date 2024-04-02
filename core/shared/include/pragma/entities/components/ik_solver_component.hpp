/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2021 Silverlan */

#ifndef __IK_SOLVER_COMPONENT_HPP__
#define __IK_SOLVER_COMPONENT_HPP__

#include "pragma/entities/components/base_entity_component.hpp"
#include "pragma/entities/components/base_entity_component_member_register.hpp"
#include "pragma/entities/components/ik_solver/rig_config.hpp"
#include "pragma/game/game_coordinate_system.hpp"
#include "pragma/util/ik.hpp"
#include <mathutil/uvec.h>

namespace pragma {
	namespace ik {
		class RigConfig;
	};
	class DLLNETWORK IkSolverComponent final : public BaseEntityComponent, public DynamicMemberRegister {
	  public:
		using IkBoneId = pragma::ik::BoneId;
		static ComponentEventId EVENT_INITIALIZE_SOLVER;
		static ComponentEventId EVENT_ON_IK_UPDATED;
		static void RegisterEvents(pragma::EntityComponentManager &componentManager, TRegisterComponentEvent registerEvent);
		static void RegisterMembers(pragma::EntityComponentManager &componentManager, TRegisterComponentMember registerMember);
		static std::optional<std::string> GetControlBoneName(const std::string &propPath);
		IkSolverComponent(BaseEntity &ent);
		virtual ~IkSolverComponent() override;
		virtual void Initialize() override;
		virtual void InitializeLuaObject(lua_State *l) override;
		virtual void OnEntitySpawn() override;

		void SetIkRigFile(const std::string &ikRigFile);
		const std::string &GetIkRigFile() const;

		void AddSkeletalBone(pragma::animation::BoneId boneId);
		void SetBoneLocked(pragma::animation::BoneId boneId, bool locked);
		pragma::ik::Bone *GetBone(pragma::animation::BoneId boneId);
		pragma::ik::IControl *GetControl(pragma::animation::BoneId boneId);
		size_t GetBoneCount() const;
		std::optional<pragma::animation::BoneId> GetSkeletalBoneId(IkBoneId boneId) const;
		std::optional<IkBoneId> GetIkBoneId(pragma::animation::BoneId boneId) const;
		std::optional<pragma::animation::BoneId> GetControlBoneId(const std::string &propPath);

		void AddDragControl(pragma::animation::BoneId boneId, float maxForce = -1.f, float rigidity = 1.f);
		void AddStateControl(pragma::animation::BoneId boneId, float maxForce = -1.f, float rigidity = 1.f);
		void AddOrientedDragControl(pragma::animation::BoneId boneId, float maxForce = -1.f, float rigidity = 1.f);

		struct DLLNETWORK ConstraintInfo {
			ConstraintInfo() = default;
			ConstraintInfo(pragma::animation::BoneId bone0, pragma::animation::BoneId bone1);
			pragma::animation::BoneId boneId0 = 0;
			pragma::animation::BoneId boneId1 = 0;
			float rigidity = 1'000.f;
			float maxForce = -1.f;
		};
		void AddFixedConstraint(const ConstraintInfo &constraintInfo);
		void AddHingeConstraint(const ConstraintInfo &constraintInfo, umath::Degree minAngle, umath::Degree maxAngle, const Quat &offsetRotation = uquat::identity(), SignedAxis twistAxis = SignedAxis::X);
		void AddBallSocketConstraint(const ConstraintInfo &constraintInfo, const EulerAngles &minLimits, const EulerAngles &maxLimits, SignedAxis twistAxis = SignedAxis::Z);

		struct DLLNETWORK JointInfo {
			JointInfo() = default;
			JointInfo(pragma::animation::BoneId bone0, pragma::animation::BoneId bone1);
			pragma::animation::BoneId boneId0 = 0;
			pragma::animation::BoneId boneId1 = 0;
			Vector3 anchorPosition;
			Vector3 axisA;
			Vector3 axisB;
			umath::Degree maxAngle = 0;
			float rigidity = 1.f;
		};
		void AddBallSocketJoint(const JointInfo &jointInfo);
		void AddSwingLimit(const JointInfo &jointInfo);
		void AddTwistLimit(const JointInfo &jointInfo);
		void AddSwivelHingeJoint(const JointInfo &jointInfo);
		void AddTwistJoint(const JointInfo &jointInfo);

		udm::PProperty &GetIkRig() { return m_ikRig; }
		void Solve();
		void ResetIkRig();

		void SetResetSolver(bool resetSolver);
		bool ShouldResetSolver() const;

		const std::shared_ptr<pragma::ik::Solver> &GetIkSolver() const;
		bool AddIkSolverByRig(const ik::RigConfig &ikRig);
		bool AddIkSolverByChain(const std::string &boneName, uint32_t chainLength);
		virtual const ComponentMemberInfo *GetMemberInfo(ComponentMemberIndex idx) const override;

		// Internal use only
		bool UpdateIkRig();
	  protected:
		static void UpdateGlobalSolverSettings();
		void UpdateSolverSettings();
		virtual std::optional<ComponentMemberIndex> DoGetMemberIndex(const std::string &name) const override;
		void ResetIkBones();
		void UpdateIkRigFile();
		void InitializeSolver();
		std::optional<umath::ScaledTransform> GetReferenceBonePose(pragma::animation::BoneId boneId) const;
		pragma::ik::Bone *AddBone(const std::string &boneName, pragma::animation::BoneId boneId, const umath::Transform &pose, float radius, float length);
		void AddControl(pragma::animation::BoneId boneId, pragma::ik::RigConfigControl::Type type, float maxForce = -1.f, float rigidity = 1.f);
		pragma::ik::Bone *GetIkBone(pragma::animation::BoneId boneId);

		bool GetConstraintBones(pragma::animation::BoneId boneId0, pragma::animation::BoneId boneId1, pragma::ik::Bone **bone0, pragma::ik::Bone **bone1, umath::ScaledTransform &pose0, umath::ScaledTransform &pose1) const;

		struct PinnedBoneInfo {
			pragma::animation::BoneId boneId;
			IkBoneId ikBoneId;
			umath::ScaledTransform oldPose;
		};

		udm::PProperty m_ikRig;
		std::string m_ikRigFile;
		std::shared_ptr<pragma::ik::Solver> m_ikSolver;
		std::unordered_map<pragma::animation::BoneId, IkBoneId> m_boneIdToIkBoneId;
		std::unordered_map<IkBoneId, pragma::animation::BoneId> m_ikBoneIdToBoneId;
		std::unordered_map<pragma::animation::BoneId, std::shared_ptr<pragma::ik::IControl>> m_ikControls;
		std::vector<PinnedBoneInfo> m_pinnedBones;
		bool m_updateRequired = false;
		bool m_resetIkPose = true;
	};
};

#endif
