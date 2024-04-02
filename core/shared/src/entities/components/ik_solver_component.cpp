/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2021 Silverlan
 */

#include "stdafx_shared.h"
#include "pragma/entities/components/ik_solver_component.hpp"
#include "pragma/entities/attribute_specialization_type.hpp"
#include "pragma/entities/entity_component_manager_t.hpp"
#include "pragma/entities/components/ik_solver/rig_config.hpp"
#include "pragma/entities/components/component_member_flags.hpp"
#include "pragma/entities/components/constraints/constraint_component.hpp"
#include "pragma/entities/components/constraints/constraint_manager_component.hpp"
#include "pragma/entities/components/base_animated_component.hpp"
#include "pragma/entities/entity_component_system_t.hpp"
#include "pragma/model/model.h"
#include "pragma/logging.hpp"
#include <pragma/entities/entity_iterator.hpp>
#include "pragma/model/animation/skeleton.hpp"
#include "pragma/model/animation/bone.hpp"

using namespace pragma;

ComponentEventId IkSolverComponent::EVENT_INITIALIZE_SOLVER = pragma::INVALID_COMPONENT_ID;
ComponentEventId IkSolverComponent::EVENT_ON_IK_UPDATED = pragma::INVALID_COMPONENT_ID;
IkSolverComponent::ConstraintInfo::ConstraintInfo(pragma::animation::BoneId bone0, pragma::animation::BoneId bone1) : boneId0 {bone0}, boneId1 {bone1} {}
IkSolverComponent::JointInfo::JointInfo(pragma::animation::BoneId bone0, pragma::animation::BoneId bone1) : boneId0 {bone0}, boneId1 {bone1} {}
void IkSolverComponent::RegisterEvents(pragma::EntityComponentManager &componentManager, TRegisterComponentEvent registerEvent)
{
	EVENT_INITIALIZE_SOLVER = registerEvent("INITIALIZE_SOLVER", ComponentEventInfo::Type::Broadcast);
	EVENT_ON_IK_UPDATED = registerEvent("ON_IK_UPDATED", ComponentEventInfo::Type::Explicit);
}
static void set_ik_rig(const ComponentMemberInfo &memberInfo, IkSolverComponent &component, const pragma::ents::Element &value) { component.UpdateIkRig(); }
static void get_ik_rig(const ComponentMemberInfo &memberInfo, IkSolverComponent &component, pragma::ents::Element &value) { value = component.GetIkRig(); }
void IkSolverComponent::RegisterMembers(pragma::EntityComponentManager &componentManager, TRegisterComponentMember registerMember)
{
	using T = IkSolverComponent;

	{
		using TRigConfig = pragma::ents::Element;
		auto memberInfo = create_component_member_info<T, TRigConfig,
		  // For some reasons these don't work as lambdas (VS compiler bug?)
		  &set_ik_rig, &get_ik_rig>("rigConfig");
		registerMember(std::move(memberInfo));
	}
	{
		using TRigConfigFile = std::string;
		auto memberInfo = create_component_member_info<T, TRigConfigFile, static_cast<void (T::*)(const TRigConfigFile &)>(&T::SetIkRigFile), static_cast<const TRigConfigFile &(T::*)() const>(&T::GetIkRigFile)>("rigConfigFile", "", AttributeSpecializationType::File);
		auto &metaData = memberInfo.AddMetaData();
		metaData["rootPath"] = "scripts/ik_rigs/";
		metaData["extensions"] = pragma::ik::RigConfig::get_supported_extensions();
		metaData["stripExtension"] = true;
		registerMember(std::move(memberInfo));
	}
	{
		using TResetSolver = bool;
		auto memberInfo = create_component_member_info<T, TResetSolver, static_cast<void (T::*)(TResetSolver)>(&T::SetResetSolver), static_cast<TResetSolver (T::*)() const>(&T::ShouldResetSolver)>("resetSolver", true);
		memberInfo.SetFlag(pragma::ComponentMemberFlags::HideInInterface);
		registerMember(std::move(memberInfo));
	}
}
IkSolverComponent::IkSolverComponent(BaseEntity &ent) : BaseEntityComponent(ent), m_ikRig {udm::Property::Create<udm::Element>()} {}
void IkSolverComponent::UpdateGlobalSolverSettings()
{
	for(auto *nw : {pragma::get_engine()->GetServerNetworkState(), pragma::get_engine()->GetClientState()}) {
		if(!nw)
			continue;
		auto *game = nw->GetGameState();
		if(!game)
			continue;
		auto cIt = EntityCIterator<pragma::IkSolverComponent> {*game};
		for(auto &solverC : cIt)
			solverC.UpdateSolverSettings();
	}
}
void IkSolverComponent::UpdateSolverSettings()
{
	auto &solver = GetIkSolver();
	if(!solver)
		return;
	auto &game = GetGame();
	solver->SetTimeStepDuration(game.GetConVarFloat("ik_solver_time_step_duration"));
	solver->SetControlIterationCount(game.GetConVarInt("ik_solver_control_iteration_count"));
	solver->SetFixerIterationCount(game.GetConVarInt("ik_solver_fixer_iteration_count"));
	solver->SetVelocitySubIterationCount(game.GetConVarInt("ik_solver_velocity_sub_iteration_count"));
}
static std::array<uint32_t, umath::to_integral(NwStateType::Count)> g_solverCount {false, false};
void IkSolverComponent::Initialize()
{
	auto &nw = GetNetworkState();
	if(g_solverCount[umath::to_integral(nw.GetType())] == 0) {
		++g_solverCount[umath::to_integral(nw.GetType())];

		// Initialize change callbacks for this network state
		nw.RegisterConVarCallback("ik_solver_time_step_duration", std::function<void(NetworkState *, const ConVar &, float, float)> {+[](NetworkState *state, const ConVar &cvar, float oldVal, float newVal) { UpdateGlobalSolverSettings(); }});
		nw.RegisterConVarCallback("ik_solver_control_iteration_count", std::function<void(NetworkState *, const ConVar &, int32_t, int32_t)> {+[](NetworkState *state, const ConVar &cvar, int32_t oldVal, int32_t newVal) { UpdateGlobalSolverSettings(); }});
		nw.RegisterConVarCallback("ik_solver_fixer_iteration_count", std::function<void(NetworkState *, const ConVar &, int32_t, int32_t)> {+[](NetworkState *state, const ConVar &cvar, int32_t oldVal, int32_t newVal) { UpdateGlobalSolverSettings(); }});
		nw.RegisterConVarCallback("ik_solver_velocity_sub_iteration_count", std::function<void(NetworkState *, const ConVar &, int32_t, int32_t)> {+[](NetworkState *state, const ConVar &cvar, int32_t oldVal, int32_t newVal) { UpdateGlobalSolverSettings(); }});
	}

	BaseEntityComponent::Initialize();
	GetEntity().AddComponent<ConstraintManagerComponent>();
	auto constraintC = GetEntity().AddComponent<ConstraintComponent>();
	constraintC->SetDrivenObject(pragma::EntityUComponentMemberRef {GetEntity(), "ik_solver", "rigConfig"});
	BindEvent(ConstraintComponent::EVENT_APPLY_CONSTRAINT, [this](std::reference_wrapper<pragma::ComponentEvent> evData) -> util::EventReply {
		Solve();
		return util::EventReply::Unhandled;
	});
}
IkSolverComponent::~IkSolverComponent() { --g_solverCount[umath::to_integral(GetNetworkState().GetType())]; }

void IkSolverComponent::InitializeSolver()
{
	m_ikControls.clear();
	m_boneIdToIkBoneId.clear();
	m_ikBoneIdToBoneId.clear();
	m_ikSolver = std::make_unique<pragma::ik::Solver>(100, 10);

	m_pinnedBones.clear();
	UpdateSolverSettings();

	ClearMembers();
	OnMembersChanged();

	BroadcastEvent(EVENT_INITIALIZE_SOLVER);
}
void IkSolverComponent::AddSkeletalBone(pragma::animation::BoneId boneId)
{
	auto &ent = GetEntity();
	auto &mdl = ent.GetModel();
	if(!mdl)
		return;
	auto &skeleton = mdl->GetSkeleton();
	auto bone = skeleton.GetBone(boneId);
	if(bone.expired())
		return;
	auto &ref = mdl->GetReference();
	umath::ScaledTransform pose;
	if(!ref.GetBonePose(boneId, pose))
		return;
	AddBone(bone.lock()->name, boneId, pose, 1.f, 1.f);
}
void IkSolverComponent::AddDragControl(pragma::animation::BoneId boneId, float maxForce, float rigidity) { AddControl(boneId, pragma::ik::RigConfigControl::Type::Drag, maxForce, rigidity); }
void IkSolverComponent::AddStateControl(pragma::animation::BoneId boneId, float maxForce, float rigidity) { AddControl(boneId, pragma::ik::RigConfigControl::Type::State, maxForce, rigidity); }
void IkSolverComponent::AddOrientedDragControl(pragma::animation::BoneId boneId, float maxForce, float rigidity) { AddControl(boneId, pragma::ik::RigConfigControl::Type::OrientedDrag, maxForce, rigidity); }
size_t IkSolverComponent::GetBoneCount() const { return m_ikSolver->GetBoneCount(); }
pragma::ik::IControl *IkSolverComponent::GetControl(pragma::animation::BoneId boneId)
{
	auto it = m_ikControls.find(boneId);
	if(it == m_ikControls.end())
		return nullptr;
	return it->second.get();
}
pragma::ik::Bone *IkSolverComponent::GetBone(pragma::animation::BoneId boneId)
{
	auto it = m_boneIdToIkBoneId.find(boneId);
	if(it == m_boneIdToIkBoneId.end())
		return nullptr;
	return m_ikSolver->GetBone(it->second);
}
void IkSolverComponent::SetBoneLocked(pragma::animation::BoneId boneId, bool locked)
{
	auto *bone = GetBone(boneId);
	if(!bone)
		return;
	bone->SetPinned(locked);
	auto it = std::find_if(m_pinnedBones.begin(), m_pinnedBones.end(), [boneId](const PinnedBoneInfo &info) { return info.boneId == boneId; });
	if(it != m_pinnedBones.end()) {
		if(!locked)
			m_pinnedBones.erase(it);
		return;
	}
	m_pinnedBones.push_back({});
	auto ikBoneId = GetIkBoneId(boneId);
	assert(ikBoneId.has_value());
	auto &info = m_pinnedBones.back();
	info.boneId = boneId;
	info.ikBoneId = *ikBoneId;
}
std::optional<umath::ScaledTransform> IkSolverComponent::GetReferenceBonePose(pragma::animation::BoneId boneId) const
{
	auto &mdl = GetEntity().GetModel();
	if(!mdl)
		return {};
	return mdl->GetReferenceBonePose(boneId);
}
bool IkSolverComponent::GetConstraintBones(pragma::animation::BoneId boneId0, pragma::animation::BoneId boneId1, pragma::ik::Bone **bone0, pragma::ik::Bone **bone1, umath::ScaledTransform &pose0, umath::ScaledTransform &pose1) const
{
	auto itBone0 = m_boneIdToIkBoneId.find(boneId0);
	auto itBone1 = m_boneIdToIkBoneId.find(boneId1);
	if(itBone0 == m_boneIdToIkBoneId.end() || itBone1 == m_boneIdToIkBoneId.end()) {
		spdlog::debug("Failed to add get constraint bones for ik solver {}: Bone {} or {} do not exist.", GetEntity().ToString(), boneId0, boneId1);
		return false;
	}
	*bone0 = m_ikSolver->GetBone(itBone0->second);
	*bone1 = m_ikSolver->GetBone(itBone1->second);
	if(!*bone0 || !*bone1) {
		spdlog::debug("Failed to add get constraint bones for ik solver {}: Bone {} or {} do not exist in solver.", GetEntity().ToString(), boneId0, boneId1);
		return false;
	}
	auto refPose0 = GetReferenceBonePose(boneId0);
	auto refPose1 = GetReferenceBonePose(boneId1);
	if(!refPose0.has_value() || !refPose1.has_value()) {
		spdlog::debug("Failed to add get constraint bones for ik solver {}: Bone {} or {} do not exist in reference pose.", GetEntity().ToString(), boneId0, boneId1);
		return false;
	}
	pose0 = *refPose0;
	pose1 = *refPose1;
	return true;
}
void IkSolverComponent::AddBallSocketJoint(const JointInfo &jointInfo)
{
	pragma::ik::Bone *bone0, *bone1;
	umath::ScaledTransform refPose0, refPose1;
	if(!GetConstraintBones(jointInfo.boneId0, jointInfo.boneId1, &bone0, &bone1, refPose0, refPose1))
		return;
	m_ikSolver->AddBallSocketJoint(*bone0, *bone1, jointInfo.anchorPosition);
}
void IkSolverComponent::AddSwingLimit(const JointInfo &jointInfo)
{
	pragma::ik::Bone *bone0, *bone1;
	umath::ScaledTransform refPose0, refPose1;
	if(!GetConstraintBones(jointInfo.boneId0, jointInfo.boneId1, &bone0, &bone1, refPose0, refPose1))
		return;
	m_ikSolver->AddSwingLimit(*bone0, *bone1, jointInfo.axisA, jointInfo.axisB, umath::deg_to_rad(jointInfo.maxAngle));
}
void IkSolverComponent::AddTwistLimit(const JointInfo &jointInfo)
{
	pragma::ik::Bone *bone0, *bone1;
	umath::ScaledTransform refPose0, refPose1;
	if(!GetConstraintBones(jointInfo.boneId0, jointInfo.boneId1, &bone0, &bone1, refPose0, refPose1))
		return;
	m_ikSolver->AddTwistLimit(*bone0, *bone1, jointInfo.axisA, jointInfo.axisB, umath::deg_to_rad(jointInfo.maxAngle));
}
void IkSolverComponent::AddSwivelHingeJoint(const JointInfo &jointInfo)
{
	pragma::ik::Bone *bone0, *bone1;
	umath::ScaledTransform refPose0, refPose1;
	if(!GetConstraintBones(jointInfo.boneId0, jointInfo.boneId1, &bone0, &bone1, refPose0, refPose1))
		return;
	m_ikSolver->AddSwivelHingeJoint(*bone0, *bone1, jointInfo.axisA, jointInfo.axisB);
}
void IkSolverComponent::AddTwistJoint(const JointInfo &jointInfo)
{
	pragma::ik::Bone *bone0, *bone1;
	umath::ScaledTransform refPose0, refPose1;
	if(!GetConstraintBones(jointInfo.boneId0, jointInfo.boneId1, &bone0, &bone1, refPose0, refPose1))
		return;
	m_ikSolver->AddTwistJoint(*bone0, *bone1, jointInfo.axisA, jointInfo.axisB);
}
bool IkSolverComponent::UpdateIkRig()
{
	udm::LinkedPropertyWrapper prop {*m_ikRig};
	auto rig = pragma::ik::RigConfig::load_from_udm_data(prop);
	if(rig)
		AddIkSolverByRig(*rig);
	OnMembersChanged();
	return true;
}
void IkSolverComponent::InitializeLuaObject(lua_State *l) { pragma::BaseLuaHandle::InitializeLuaObject<std::remove_reference_t<decltype(*this)>>(l); }

void IkSolverComponent::OnEntitySpawn()
{
	SetTickPolicy(pragma::TickPolicy::Always);
	InitializeSolver();
}
std::optional<IkSolverComponent::IkBoneId> IkSolverComponent::GetIkBoneId(pragma::animation::BoneId boneId) const
{
	auto it = m_boneIdToIkBoneId.find(boneId);
	if(it == m_boneIdToIkBoneId.end())
		return {};
	return it->second;
}
std::optional<std::string> IkSolverComponent::GetControlBoneName(const std::string &propPath)
{
	auto path = util::Path::CreatePath(propPath);
	size_t nextOffset = 0;
	if(path.GetComponent(0, &nextOffset) != "control")
		return {};
	auto boneName = path.GetComponent(nextOffset);
	if(boneName.empty())
		return {};
	return std::string {boneName};
}
std::optional<pragma::animation::BoneId> IkSolverComponent::GetControlBoneId(const std::string &propPath)
{
	auto boneName = GetControlBoneName(propPath);
	if(!boneName)
		return {};
	auto &mdl = GetEntity().GetModel();
	if(!mdl)
		return {};
	auto &skeleton = mdl->GetSkeleton();
	auto boneId = skeleton.LookupBone(*boneName);
	if(boneId == -1)
		return {};
	return boneId;
}
std::optional<pragma::animation::BoneId> IkSolverComponent::GetSkeletalBoneId(IkBoneId boneId) const
{
	auto it = m_ikBoneIdToBoneId.find(boneId);
	if(it == m_ikBoneIdToBoneId.end())
		return {};
	return it->second;
}
pragma::ik::Bone *IkSolverComponent::GetIkBone(pragma::animation::BoneId boneId)
{
	auto rigConfigBoneId = GetIkBoneId(boneId);
	if(!rigConfigBoneId.has_value())
		return nullptr;
	return m_ikSolver->GetBone(*rigConfigBoneId);
}
void IkSolverComponent::AddControl(pragma::animation::BoneId boneId, pragma::ik::RigConfigControl::Type type, float maxForce, float rigidity)
{
	auto &mdl = GetEntity().GetModel();
	if(!mdl) {
		spdlog::debug("Failed to add control to ik solver {}: Entity has no model.", GetEntity().ToString());
		return;
	}
	auto &skeleton = mdl->GetSkeleton();
	auto bone = skeleton.GetBone(boneId).lock();
	if(!bone) {
		spdlog::debug("Failed to add control to ik solver {}: Control bone {} does not exist in skeleton.", GetEntity().ToString(), boneId);
		return;
	}
	auto rigConfigBoneId = GetIkBoneId(boneId);
	if(rigConfigBoneId.has_value() == false) {
		spdlog::debug("Failed to add control to ik solver {}: Control bone {} does not exist in ik rig.", GetEntity().ToString(), boneId);
		return;
	}
	auto *rigConfigBone = m_ikSolver->GetBone(*rigConfigBoneId);
	if(!rigConfigBone) {
		spdlog::debug("Failed to add control to ik solver {}: Control bone {} does not exist in ik rig.", GetEntity().ToString(), boneId);
		return;
	}
	if(m_ikSolver->FindControl(*rigConfigBone) != nullptr)
		return;
	pragma::ik::IControl *control = nullptr;
	switch(type) {
	case pragma::ik::RigConfigControl::Type::State:
		{
			auto &stateControl = m_ikSolver->AddStateControl(*rigConfigBone);
			stateControl.SetTargetPosition(rigConfigBone->GetPos());
			stateControl.SetTargetOrientation(rigConfigBone->GetRot());
			control = &stateControl;
			break;
		}
	case pragma::ik::RigConfigControl::Type::Drag:
		{
			auto &dragControl = m_ikSolver->AddDragControl(*rigConfigBone);
			dragControl.SetTargetPosition(rigConfigBone->GetPos());
			control = &dragControl;
			break;
		}
	case pragma::ik::RigConfigControl::Type::OrientedDrag:
		{
			auto &dragControl = m_ikSolver->AddOrientedDragControl(*rigConfigBone);
			dragControl.SetTargetPosition(rigConfigBone->GetPos());
			dragControl.SetTargetOrientation(rigConfigBone->GetRot());
			control = &dragControl;
			break;
		}
	}
	static_assert(umath::to_integral(pragma::ik::RigConfigControl::Type::Count) == 3u);

	control->SetMaxForce((maxForce < 0.f) ? std::numeric_limits<float>::max() : maxForce);
	control->SetRigidity(rigidity);
	std::string name = bone->name;
	using TComponent = IkSolverComponent;
	auto defGetSet = [this, &bone, rigConfigBone, &name, boneId](auto &ctrl) {
		using TControl = std::remove_reference_t<decltype(ctrl)>;

		auto posePropName = "control/" + name + "/pose";
		auto posPropName = "control/" + name + "/position";
		auto rotPropName = "control/" + name + "/rotation";

		auto coordMetaData = std::make_shared<ents::CoordinateTypeMetaData>();
		coordMetaData->space = umath::CoordinateSpace::Object;

		constexpr auto hasRotation = std::is_same_v<TControl, pragma::ik::OrientedDragControl> || std::is_same_v<TControl, pragma::ik::StateControl>;

		std::shared_ptr<ents::PoseComponentTypeMetaData> compMetaData = nullptr;
		if constexpr(hasRotation) {
			compMetaData = std::make_shared<ents::PoseComponentTypeMetaData>();
			compMetaData->poseProperty = posePropName;
		}

		auto memberInfoPos = pragma::ComponentMemberInfo::CreateDummy();
		memberInfoPos.SetName(posPropName);
		memberInfoPos.type = ents::EntityMemberType::Vector3;
		memberInfoPos.userIndex = boneId;
		memberInfoPos.AddTypeMetaData(coordMetaData);
		if(compMetaData)
			memberInfoPos.AddTypeMetaData(compMetaData);
		memberInfoPos.SetFlag(pragma::ComponentMemberFlags::ObjectSpace);
		using TValue = Vector3;
		memberInfoPos.SetGetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, TValue &outValue) {
			auto it = component.m_ikControls.find(memberInfo.userIndex);
			if(it == component.m_ikControls.end()) {
				outValue = {};
				return;
			}
			outValue = static_cast<TControl *>(it->second.get())->GetTargetPosition();
		})>();
		memberInfoPos.SetSetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, const TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, const TValue &value) {
			auto it = component.m_ikControls.find(memberInfo.userIndex);
			if(it == component.m_ikControls.end())
				return;
			static_cast<TControl *>(it->second.get())->SetTargetPosition(value);
			component.m_updateRequired = true;
		})>();
		RegisterMember(std::move(memberInfoPos));
		ctrl.SetTargetPosition(rigConfigBone->GetPos());

		if constexpr(hasRotation) {
			auto memberInfoRot = pragma::ComponentMemberInfo::CreateDummy();
			memberInfoRot.SetName(rotPropName);
			memberInfoRot.type = ents::EntityMemberType::Quaternion;
			memberInfoRot.userIndex = boneId;
			memberInfoRot.AddTypeMetaData(coordMetaData);
			memberInfoRot.AddTypeMetaData(compMetaData);
			memberInfoRot.SetFlag(pragma::ComponentMemberFlags::ObjectSpace);
			using TValue = Quat;
			memberInfoRot.SetGetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, TValue &outValue) {
				auto it = component.m_ikControls.find(memberInfo.userIndex);
				if(it == component.m_ikControls.end()) {
					outValue = {};
					return;
				}
				outValue = static_cast<TControl *>(it->second.get())->GetTargetOrientation();
			})>();
			memberInfoRot.SetSetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, const TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, const TValue &value) {
				auto it = component.m_ikControls.find(memberInfo.userIndex);
				if(it == component.m_ikControls.end())
					return;
				static_cast<TControl *>(it->second.get())->SetTargetOrientation(value);
				component.m_updateRequired = true;
			})>();
			RegisterMember(std::move(memberInfoRot));
			ctrl.SetTargetOrientation(rigConfigBone->GetRot());

			auto poseMetaData = std::make_shared<ents::PoseTypeMetaData>();
			poseMetaData->posProperty = posPropName;
			poseMetaData->rotProperty = rotPropName;

			auto memberInfoPose = pragma::ComponentMemberInfo::CreateDummy();
			memberInfoPose.SetName(posePropName);
			memberInfoPose.type = ents::EntityMemberType::Transform;
			memberInfoPose.userIndex = boneId;
			memberInfoPose.AddTypeMetaData(coordMetaData);
			memberInfoPose.AddTypeMetaData(poseMetaData);
			memberInfoPose.SetFlag(pragma::ComponentMemberFlags::ObjectSpace);
			using TValuePose = umath::Transform;
			memberInfoPose.SetGetterFunction<TComponent, TValuePose, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, TValuePose &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, TValuePose &outValue) {
				auto it = component.m_ikControls.find(memberInfo.userIndex);
				if(it == component.m_ikControls.end()) {
					outValue = {};
					return;
				}
				auto *ctrl = static_cast<TControl *>(it->second.get());
				outValue = {ctrl->GetTargetPosition(), ctrl->GetTargetOrientation()};
			})>();
			memberInfoPose.SetSetterFunction<TComponent, TValuePose, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, const TValuePose &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, const TValuePose &value) {
				auto it = component.m_ikControls.find(memberInfo.userIndex);
				if(it == component.m_ikControls.end())
					return;
				auto *ctrl = static_cast<TControl *>(it->second.get());
				ctrl->SetTargetPosition(value.GetOrigin());
				ctrl->SetTargetOrientation(value.GetRotation());
				component.m_updateRequired = true;
			})>();
			RegisterMember(std::move(memberInfoPose));
		}
		//}
	};
	switch(type) {
	case pragma::ik::RigConfigControl::Type::State:
		defGetSet(static_cast<pragma::ik::StateControl &>(*control));
		break;
	case pragma::ik::RigConfigControl::Type::Drag:
		defGetSet(static_cast<pragma::ik::DragControl &>(*control));
		break;
	case pragma::ik::RigConfigControl::Type::OrientedDrag:
		defGetSet(static_cast<pragma::ik::OrientedDragControl &>(*control));
		break;
	}
	static_assert(umath::to_integral(pragma::ik::RigConfigControl::Type::Count) == 3u);

#if 0
	auto memberInfoLocked = pragma::ComponentMemberInfo::CreateDummy();
	memberInfoLocked.SetName("control/" + name + "/locked");
	memberInfoLocked.type = ents::EntityMemberType::Boolean;
	memberInfoLocked.userIndex = boneId;
	using TValue = bool;
	memberInfoLocked.SetGetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, TValue &outValue) {
		auto it = component.m_boneIdToIkBoneId.find(memberInfo.userIndex);
		if(it == component.m_boneIdToIkBoneId.end()) {
			outValue = false;
			return;
		}
		auto *bone = component.m_ikSolver->GetBone(it->second);
		if(!bone) {
			outValue = false;
			return;
		}
		outValue = bone->IsPinned();
	})>();
	memberInfoLocked.SetSetterFunction<TComponent, TValue, static_cast<void (*)(const pragma::ComponentMemberInfo &, TComponent &, const TValue &)>([](const pragma::ComponentMemberInfo &memberInfo, TComponent &component, const TValue &value) {
		auto it = component.m_boneIdToIkBoneId.find(memberInfo.userIndex);
		if(it == component.m_boneIdToIkBoneId.end())
			return;
		auto *bone = component.m_ikSolver->GetBone(it->second);
		if(!bone)
			return;
		bone->SetPinned(value);
	})>();
	RegisterMember(std::move(memberInfoLocked));
#endif

	// TODO: Position weight and rotation weight

	m_ikControls[boneId] = m_ikSolver->FindControlPtr(*rigConfigBone);
}
const ComponentMemberInfo *IkSolverComponent::GetMemberInfo(ComponentMemberIndex idx) const
{
	auto numStatic = GetStaticMemberCount();
	if(idx < numStatic)
		return BaseEntityComponent::GetMemberInfo(idx);
	return DynamicMemberRegister::GetMemberInfo(idx);
}

std::optional<ComponentMemberIndex> IkSolverComponent::DoGetMemberIndex(const std::string &name) const
{
	auto idx = BaseEntityComponent::DoGetMemberIndex(name);
	if(idx.has_value())
		return idx;
	idx = DynamicMemberRegister::GetMemberIndex(name);
	if(idx.has_value())
		return *idx; // +GetStaticMemberCount();
	return std::optional<ComponentMemberIndex> {};
}
pragma::ik::Bone *IkSolverComponent::AddBone(const std::string &boneName, pragma::animation::BoneId boneId, const umath::Transform &pose, float radius, float length)
{
	auto rigConfigBone = GetIkBone(boneId);
	if(rigConfigBone)
		return rigConfigBone;
	IkBoneId rigConfigBoneId;
	rigConfigBone = &m_ikSolver->AddBone(pose.GetOrigin(), pose.GetRotation(), radius, length, &rigConfigBoneId);
	rigConfigBone->SetName(boneName);
	m_boneIdToIkBoneId[boneId] = rigConfigBoneId;
	m_ikBoneIdToBoneId[rigConfigBoneId] = boneId;
	return rigConfigBone;
}
void IkSolverComponent::SetResetSolver(bool resetSolver) { m_resetIkPose = resetSolver; }
bool IkSolverComponent::ShouldResetSolver() const { return m_resetIkPose; }
void IkSolverComponent::ResetIkRig() { InitializeSolver(); }
const std::shared_ptr<pragma::ik::Solver> &IkSolverComponent::GetIkSolver() const { return m_ikSolver; }

void IkSolverComponent::Solve()
{
	if(!m_updateRequired) {
		auto animC = GetEntity().GetAnimatedComponent();
		for(auto &info : m_pinnedBones) {
			Vector3 pos;
			Quat rot;
			Vector3 scale;
			if(animC.valid() && animC->GetBonePose(info.boneId, &pos, &rot, &scale, umath::CoordinateSpace::Object)) {
				if(uvec::distance_sqr(pos, info.oldPose.GetOrigin()) > 0.00001f || uquat::dot_product(rot, info.oldPose.GetRotation()) < 0.99999f || uvec::distance_sqr(scale, info.oldPose.GetScale()) > 0.00001f) {
					m_updateRequired = true;
				}
			}
		}
	}

	if(!m_updateRequired) {
		InvokeEventCallbacks(EVENT_ON_IK_UPDATED);
		return;
	}
	m_updateRequired = false;

	{
		auto animC = GetEntity().GetAnimatedComponent();
		for(auto &info : m_pinnedBones) {
			auto animC = GetEntity().GetAnimatedComponent(); // TODO: This is not very fast...
			Vector3 pos;
			Quat rot;
			Vector3 scale;
			if(animC.valid() && animC->GetBonePose(info.boneId, &pos, &rot, &scale, umath::CoordinateSpace::Object)) {
				auto *ikBone = m_ikSolver->GetBone(info.ikBoneId);
				if(ikBone) {
					ikBone->SetPos(pos);
					ikBone->SetRot(rot);
				}
				info.oldPose = {pos, rot, scale};
			}
		}
	}

	if(m_resetIkPose)
		ResetIkBones();
	m_ikSolver->Solve();
	InvokeEventCallbacks(EVENT_ON_IK_UPDATED);
}
void IkSolverComponent::ResetIkBones()
{
	auto numBones = m_ikSolver->GetBoneCount();
	for(auto i = decltype(numBones) {0u}; i < numBones; ++i) {
		auto *bone = m_ikSolver->GetBone(i);
		if(!bone || bone->IsPinned())
			continue; // Pinned bones are handled via forward kinematics
		auto it = m_ikBoneIdToBoneId.find(i);
		if(it == m_ikBoneIdToBoneId.end())
			continue;
		auto boneId = it->second;
		auto pose = GetReferenceBonePose(boneId);
		if(!pose.has_value())
			continue;
		bone->SetPos(pose->GetOrigin());
		bone->SetRot(pose->GetRotation());
	}
}
