/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2020 Florian Weischer
 */

#include "stdafx_client.h"
#include "pragma/entities/components/c_scene_component.hpp"
#include "pragma/entities/environment/lights/c_env_shadow.hpp"
#include "pragma/entities/entity_instance_index_buffer.hpp"
#include "pragma/entities/components/c_render_component.hpp"
#include "pragma/console/c_cvar.h"
#include "pragma/rendering/shaders/world/c_shader_textured.hpp"
#include "pragma/rendering/shaders/c_shader_shadow.hpp"
#include "pragma/rendering/shaders/post_processing/c_shader_hdr.hpp"
#include "pragma/rendering/shaders/post_processing/c_shader_pp_fog.hpp"
#include "pragma/rendering/shaders/post_processing/c_shader_pp_hdr.hpp"
#include "pragma/rendering/shaders/c_shader_forwardp_light_culling.hpp"
#include "pragma/rendering/renderers/base_renderer.hpp"
#include "pragma/rendering/renderers/rasterization_renderer.hpp"
#include "pragma/rendering/render_queue.hpp"
#include "pragma/entities/c_entityfactories.h"
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/entities/entity_iterator.hpp>
#include <prosper_command_buffer.hpp>

using namespace pragma;

extern DLLCLIENT CGame *c_game;
extern DLLCENGINE CEngine *c_engine;

LINK_ENTITY_TO_CLASS(scene,CScene);

CSceneComponent::CSMCascadeDescriptor::CSMCascadeDescriptor()
{}

///////////////////////////

ShaderMeshContainer::ShaderMeshContainer(pragma::ShaderGameWorldLightingPass *shader)
	: shader(shader->GetHandle())
{}

///////////////////////////

CSceneComponent::CreateInfo::CreateInfo()
	: sampleCount{static_cast<prosper::SampleCountFlags>(c_game->GetMSAASampleCount())}
{}

///////////////////////////

void CSceneComponent::RegisterEvents(pragma::EntityComponentManager &componentManager)
{
	EVENT_ON_ACTIVE_CAMERA_CHANGED = componentManager.RegisterEvent("ON_ACTIVE_CAMERA_CHANGED");
	EVENT_ON_BUILD_RENDER_QUEUES = componentManager.RegisterEvent("ON_BUILD_RENDER_QUEUES",typeid(CSceneComponent));
	EVENT_POST_RENDER_PREPASS = componentManager.RegisterEvent("POST_RENDER_PREPASS",typeid(CSceneComponent));
}

static std::shared_ptr<rendering::EntityInstanceIndexBuffer> g_entityInstanceIndexBuffer = nullptr;
const std::shared_ptr<rendering::EntityInstanceIndexBuffer> &CSceneComponent::GetEntityInstanceIndexBuffer()
{
	return g_entityInstanceIndexBuffer;
}

void CSceneComponent::UpdateRenderBuffers(const std::shared_ptr<prosper::IPrimaryCommandBuffer> &drawCmd,const rendering::RenderQueue &renderQueue,RenderPassStats *optStats)
{
	renderQueue.WaitForCompletion(optStats);
	CSceneComponent::GetEntityInstanceIndexBuffer()->UpdateBufferData(renderQueue);
	auto curEntity = std::numeric_limits<EntityIndex>::max();
	for(auto &item : renderQueue.queue)
	{
		if(item.entity == curEntity)
			continue;
		curEntity = item.entity;
		auto &ent = static_cast<CBaseEntity&>(*c_game->GetEntityByLocalIndex(item.entity));
		auto &renderC = *ent.GetRenderComponent();
		if(optStats && umath::is_flag_set(renderC.GetStateFlags(),CRenderComponent::StateFlags::RenderBufferDirty))
			(*optStats)->Increment(RenderPassStats::Counter::EntityBufferUpdates);
		auto *animC = renderC.GetAnimatedComponent();
		if(animC && animC->AreSkeletonUpdateCallbacksEnabled())
			animC->UpdateBoneMatricesMT();
		renderC.UpdateRenderBuffers(drawCmd);
	}
}

using SceneCount = uint32_t;
static std::array<SceneCount,32> g_sceneUseCount {};
static std::array<CSceneComponent*,32> g_scenes {};

ComponentEventId CSceneComponent::EVENT_ON_ACTIVE_CAMERA_CHANGED = INVALID_COMPONENT_ID;
ComponentEventId CSceneComponent::EVENT_ON_BUILD_RENDER_QUEUES = INVALID_COMPONENT_ID;
ComponentEventId CSceneComponent::EVENT_POST_RENDER_PREPASS = INVALID_COMPONENT_ID;
CSceneComponent *CSceneComponent::Create(const CreateInfo &createInfo,CSceneComponent *optParent)
{
	SceneIndex sceneIndex;
	if(optParent)
		sceneIndex = optParent->GetSceneIndex();
	else
	{
		// Find unused slot
		auto it = std::find(g_sceneUseCount.begin(),g_sceneUseCount.end(),0);
		if(it == g_sceneUseCount.end())
			return nullptr;
		sceneIndex = (it -g_sceneUseCount.begin());
	}
	auto *scene = c_game->CreateEntity<CScene>();
	if(scene == nullptr)
		return nullptr;
	auto sceneC = scene->GetComponent<CSceneComponent>();
	if(sceneC.expired())
	{
		scene->Remove();
		return nullptr;
	}
	++g_sceneUseCount.at(sceneIndex);
	sceneC->Setup(createInfo,sceneIndex);
	if(optParent == nullptr)
		g_scenes.at(sceneIndex) = sceneC.get();
	else
		umath::set_flag(sceneC->m_stateFlags,StateFlags::HasParentScene);

	scene->Spawn();
	return sceneC.get();
}

static uint32_t g_numScenes = 0;
CSceneComponent::CSceneComponent(BaseEntity &ent)
	: BaseEntityComponent{ent},m_sceneRenderDesc{*this}
{
	++g_numScenes;
}
CSceneComponent::~CSceneComponent()
{
	ClearWorldEnvironment();
	//c_engine->FlushCommandBuffers(); // We need to make sure all rendering commands have been completed, in case this scene is still in use somewhere // prosper TODO
}
void CSceneComponent::OnRemove()
{
	BaseEntityComponent::OnRemove();
	if(m_cbLink.IsValid())
		m_cbLink.Remove();

	auto sceneIndex = GetSceneIndex();
	if(sceneIndex == std::numeric_limits<SceneIndex>::max())
		return;
	assert(g_sceneUseCount > 0);
	--g_sceneUseCount.at(sceneIndex);
	if(umath::is_flag_set(m_stateFlags,StateFlags::HasParentScene) == false)
	{
		g_scenes.at(sceneIndex) = nullptr;

		// Clear all entities from this scene
		std::vector<CBaseEntity*> *ents;
		c_game->GetEntities(&ents);
		for(auto *ent : *ents)
		{
			if(ent == nullptr)
				continue;
			ent->RemoveFromScene(*this);
		}
	}

	if(--g_numScenes == 0)
		g_entityInstanceIndexBuffer = nullptr;
}
luabind::object CSceneComponent::InitializeLuaObject(lua_State *l) {return BaseEntityComponent::InitializeLuaObject<CSceneComponentHandleWrapper>(l);}
void CSceneComponent::Initialize()
{
	BaseEntityComponent::Initialize();
}


void CSceneComponent::Setup(const CreateInfo &createInfo,SceneIndex sceneIndex)
{
	m_sceneIndex = sceneIndex;
	for(auto i=decltype(pragma::CShadowCSMComponent::MAX_CASCADE_COUNT){0};i<pragma::CShadowCSMComponent::MAX_CASCADE_COUNT;++i)
		m_csmDescriptors.push_back(std::unique_ptr<CSMCascadeDescriptor>(new CSMCascadeDescriptor()));
	InitializeCameraBuffer();
	InitializeFogBuffer();
	InitializeDescriptorSetLayouts();

	InitializeRenderSettingsBuffer();
	InitializeSwapDescriptorBuffers();
	InitializeShadowDescriptorSet();

	if(g_entityInstanceIndexBuffer == nullptr)
		g_entityInstanceIndexBuffer = std::make_shared<rendering::EntityInstanceIndexBuffer>();
}

void CSceneComponent::Link(const CSceneComponent &other)
{
	auto &hCam = other.GetActiveCamera();
	if(hCam.valid())
		SetActiveCamera(*hCam.get());
	else
		SetActiveCamera();

	auto *renderer = const_cast<CSceneComponent&>(other).GetRenderer();
	SetRenderer(renderer);

	// m_sceneRenderDesc.SetOcclusionCullingHandler(const_cast<pragma::OcclusionCullingHandler&>(other.m_sceneRenderDesc.GetOcclusionCullingHandler()).shared_from_this());

	auto *occlusionCuller = const_cast<CSceneComponent&>(other).FindOcclusionCuller();
	if(occlusionCuller)
		static_cast<CBaseEntity&>(occlusionCuller->GetEntity()).AddToScene(*this);

	auto *worldEnv = other.GetWorldEnvironment();
	if(worldEnv)
		SetWorldEnvironment(*worldEnv);

	if(m_cbLink.IsValid())
		m_cbLink.Remove();
	m_cbLink = const_cast<CSceneComponent&>(other).AddEventCallback(EVENT_ON_ACTIVE_CAMERA_CHANGED,[this,&other](std::reference_wrapper<pragma::ComponentEvent> evData) -> util::EventReply {
		auto &hCam = other.GetActiveCamera();
		if(hCam.valid())
			SetActiveCamera(*hCam.get());
		else
			SetActiveCamera();
		return util::EventReply::Unhandled;
	});
}

void CSceneComponent::InitializeShadowDescriptorSet()
{
	if(pragma::ShaderGameWorldLightingPass::DESCRIPTOR_SET_SHADOWS.IsValid())
	{
		auto &context = c_engine->GetRenderContext();
		m_shadowDsg = context.CreateDescriptorSetGroup(pragma::ShaderGameWorldLightingPass::DESCRIPTOR_SET_SHADOWS);
		auto &cubeTex = context.GetDummyCubemapTexture();
		auto n = umath::to_integral(GameLimits::MaxActiveShadowCubeMaps);
		for(auto i=decltype(n){0u};i<n;++i)
			m_shadowDsg->GetDescriptorSet()->SetBindingArrayTexture(*cubeTex,umath::to_integral(pragma::ShaderSceneLit::ShadowBinding::ShadowCubeMaps),i);
	}
}

pragma::CSceneComponent *CSceneComponent::GetByIndex(SceneIndex sceneIndex)
{
	return (sceneIndex < g_scenes.size()) ? g_scenes.at(sceneIndex) : nullptr;
}

uint32_t CSceneComponent::GetSceneFlag(SceneIndex sceneIndex)
{
	return 1<<sceneIndex;
}

CSceneComponent::SceneIndex CSceneComponent::GetSceneIndex(uint32_t flag)
{
	return umath::get_least_significant_set_bit_index(flag);
}

static CVar cvShadowmapSize = GetClientConVar("cl_render_shadow_resolution");
static CVar cvShaderQuality = GetClientConVar("cl_render_shader_quality");
void CSceneComponent::InitializeRenderSettingsBuffer()
{
	// Initialize Render Settings
	auto szShadowMap = cvShadowmapSize->GetFloat();
	auto w = GetWidth();
	auto h = GetHeight();

	m_renderSettings.ambientColor = Vector4(1.f,1.f,1.f,1.f);
	m_renderSettings.posCam = Vector3(0.f,0.f,0.f);
	m_renderSettings.flags = umath::to_integral(FRenderSetting::None);
	m_renderSettings.shadowRatioX = 1.f /szShadowMap;
	m_renderSettings.shadowRatioY = 1.f /szShadowMap;
	m_renderSettings.shaderQuality = cvShaderQuality->GetInt();

	if(m_renderer.valid())
		m_renderer->UpdateRenderSettings();

	prosper::util::BufferCreateInfo createInfo {};
	createInfo.memoryFeatures = prosper::MemoryFeatureFlags::GPUBulk;
	createInfo.size = sizeof(m_renderSettings);
	createInfo.usageFlags = prosper::BufferUsageFlags::UniformBufferBit | prosper::BufferUsageFlags::TransferDstBit;
	m_renderSettingsBuffer = c_engine->GetRenderContext().CreateBuffer(createInfo,&m_renderSettings);
	m_renderSettingsBuffer->SetDebugName("render_settings_buf");
	UpdateRenderSettings();
	//
}
void CSceneComponent::InitializeCameraBuffer()
{
	// Camera
	m_cameraData.P = umat::identity();
	m_cameraData.V = umat::identity();
	m_cameraData.VP = umat::identity();

	prosper::util::BufferCreateInfo createInfo {};
	createInfo.memoryFeatures = prosper::MemoryFeatureFlags::GPUBulk;
	createInfo.size = sizeof(m_cameraData);
	createInfo.usageFlags = prosper::BufferUsageFlags::UniformBufferBit | prosper::BufferUsageFlags::TransferDstBit;
	m_cameraBuffer = c_engine->GetRenderContext().CreateBuffer(createInfo,&m_cameraData);
	m_cameraBuffer->SetDebugName("camera_buf");
	//

	// View Camera
	m_cameraViewBuffer = c_engine->GetRenderContext().CreateBuffer(createInfo,&m_cameraData);
	m_cameraViewBuffer->SetDebugName("camera_view_buf");
	//
}
void CSceneComponent::InitializeFogBuffer()
{
	prosper::util::BufferCreateInfo createInfo {};
	createInfo.memoryFeatures = prosper::MemoryFeatureFlags::GPUBulk;
	createInfo.size = sizeof(m_fogData);
	createInfo.usageFlags = prosper::BufferUsageFlags::UniformBufferBit | prosper::BufferUsageFlags::TransferDstBit;
	m_fogBuffer = c_engine->GetRenderContext().CreateBuffer(createInfo,&m_cameraData);
	m_fogBuffer->SetDebugName("fog_buf");
}
pragma::COcclusionCullerComponent *CSceneComponent::FindOcclusionCuller()
{
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::COcclusionCullerComponent>>();
	entIt.AttachFilter<EntityIteratorFilterUser>([this](BaseEntity &ent) -> bool {
		return static_cast<CBaseEntity&>(ent).IsInScene(*this);
	});
	auto it = entIt.begin();
	auto *ent = (it != entIt.end()) ? *it : nullptr;
	return ent ? ent->GetComponent<pragma::COcclusionCullerComponent>().get() : nullptr;
}
const pragma::COcclusionCullerComponent *CSceneComponent::FindOcclusionCuller() const {return const_cast<CSceneComponent*>(this)->FindOcclusionCuller();}
const std::shared_ptr<prosper::IBuffer> &CSceneComponent::GetFogBuffer() const {return m_fogBuffer;}
void CSceneComponent::UpdateCameraBuffer(std::shared_ptr<prosper::IPrimaryCommandBuffer> &drawCmd,bool bView)
{
	auto &cam = GetActiveCamera();
	if(cam.expired())
		return;
	auto &bufCam = (bView == true) ? GetViewCameraBuffer() : GetCameraBuffer();
	auto &v = cam->GetViewMatrix();
	auto &p = (bView == true) ? pragma::CCameraComponent::CalcProjectionMatrix(c_game->GetViewModelFOV(),cam->GetAspectRatio(),cam->GetNearZ(),cam->GetFarZ()) : cam->GetProjectionMatrix();
	m_cameraData.V = v;
	m_cameraData.P = p;
	m_cameraData.VP = p *v;

	if(bView == false && m_renderer.valid())
		m_renderer->UpdateCameraData(*this,m_cameraData);

	drawCmd->RecordBufferBarrier(
		*bufCam,
		prosper::PipelineStageFlags::FragmentShaderBit | prosper::PipelineStageFlags::VertexShaderBit | prosper::PipelineStageFlags::GeometryShaderBit | prosper::PipelineStageFlags::ComputeShaderBit,prosper::PipelineStageFlags::TransferBit,
		prosper::AccessFlags::ShaderReadBit,prosper::AccessFlags::TransferWriteBit
	);
	drawCmd->RecordUpdateBuffer(*bufCam,0ull,m_cameraData);
}
void CSceneComponent::UpdateBuffers(std::shared_ptr<prosper::IPrimaryCommandBuffer> &drawCmd)
{
	UpdateCameraBuffer(drawCmd,false);
	UpdateCameraBuffer(drawCmd,true);

	// Update Render Buffer
	auto &cam = GetActiveCamera();
	auto camPos = cam.valid() ? cam->GetEntity().GetPosition() : Vector3{};
	m_renderSettings.posCam = camPos;

	drawCmd->RecordBufferBarrier(
		*m_renderSettingsBuffer,
		prosper::PipelineStageFlags::FragmentShaderBit | prosper::PipelineStageFlags::VertexShaderBit | prosper::PipelineStageFlags::GeometryShaderBit | prosper::PipelineStageFlags::ComputeShaderBit,prosper::PipelineStageFlags::TransferBit,
		prosper::AccessFlags::ShaderReadBit,prosper::AccessFlags::TransferWriteBit
	);
	drawCmd->RecordUpdateBuffer(*m_renderSettingsBuffer,0ull,m_renderSettings);
	// prosper TODO: Move camPos to camera buffer, and don't update render settings buffer every frame (update when needed instead)

	if(m_renderer.valid())
		m_renderer->UpdateRendererBuffer(drawCmd);
}
void CSceneComponent::RecordRenderCommandBuffers(const util::DrawSceneInfo &drawSceneInfo)
{
	auto *renderer = GetRenderer();
	if(renderer == nullptr)
		return;
	renderer->RecordCommandBuffers(drawSceneInfo);
}
void CSceneComponent::InitializeDescriptorSetLayouts()
{
	/*auto &context = c_engine->GetRenderContext();
	m_descSetLayoutCamGraphics = Vulkan::DescriptorSetLayout::Create(context,{
		{prosper::DescriptorType::UniformBuffer,prosper::ShaderStageFlags::FragmentBit | prosper::ShaderStageFlags::VertexBit}, // Camera
		{prosper::DescriptorType::UniformBuffer,prosper::ShaderStageFlags::FragmentBit | prosper::ShaderStageFlags::VertexBit}, // Render Settings
		{prosper::DescriptorType::CombinedImageSampler,prosper::ShaderStageFlags::FragmentBit} // SSAO Map
	});
	m_descSetLayoutCamCompute = Vulkan::DescriptorSetLayout::Create(context,{
		{prosper::DescriptorType::UniformBuffer,prosper::ShaderStageFlags::ComputeBit}, // Camera
		{prosper::DescriptorType::UniformBuffer,prosper::ShaderStageFlags::ComputeBit} // Render Settings
	});*/ // prosper TODO
}
void CSceneComponent::InitializeSwapDescriptorBuffers()
{
	if(pragma::ShaderGameWorldLightingPass::DESCRIPTOR_SET_SCENE.IsValid() == false || pragma::ShaderPPFog::DESCRIPTOR_SET_FOG.IsValid() == false)
		return;
	m_camDescSetGroupGraphics = c_engine->GetRenderContext().CreateDescriptorSetGroup(pragma::ShaderGameWorldLightingPass::DESCRIPTOR_SET_SCENE);
	auto &descSetGraphics = *m_camDescSetGroupGraphics->GetDescriptorSet();
	descSetGraphics.SetBindingUniformBuffer(
		*m_cameraBuffer,umath::to_integral(pragma::ShaderGameWorldLightingPass::SceneBinding::Camera)
	);
	descSetGraphics.SetBindingUniformBuffer(
		*m_renderSettingsBuffer,umath::to_integral(pragma::ShaderGameWorldLightingPass::SceneBinding::RenderSettings)
	);

	m_camDescSetGroupCompute = c_engine->GetRenderContext().CreateDescriptorSetGroup(pragma::ShaderForwardPLightCulling::DESCRIPTOR_SET_SCENE);
	auto &descSetCompute = *m_camDescSetGroupCompute->GetDescriptorSet();
	descSetCompute.SetBindingUniformBuffer(
		*m_cameraBuffer,umath::to_integral(pragma::ShaderForwardPLightCulling::CameraBinding::Camera)
	);
	descSetCompute.SetBindingUniformBuffer(
		*m_renderSettingsBuffer,umath::to_integral(pragma::ShaderForwardPLightCulling::CameraBinding::RenderSettings)
	);

	m_camViewDescSetGroup = c_engine->GetRenderContext().CreateDescriptorSetGroup(pragma::ShaderGameWorldLightingPass::DESCRIPTOR_SET_SCENE);
	auto &descSetViewGraphics = *m_camViewDescSetGroup->GetDescriptorSet();
	descSetViewGraphics.SetBindingUniformBuffer(
		*m_cameraViewBuffer,umath::to_integral(pragma::ShaderGameWorldLightingPass::SceneBinding::Camera)
	);
	descSetViewGraphics.SetBindingUniformBuffer(
		*m_renderSettingsBuffer,umath::to_integral(pragma::ShaderGameWorldLightingPass::SceneBinding::RenderSettings)
	);

	m_fogDescSetGroup = c_engine->GetRenderContext().CreateDescriptorSetGroup(pragma::ShaderPPFog::DESCRIPTOR_SET_FOG);
	m_fogDescSetGroup->GetDescriptorSet()->SetBindingUniformBuffer(*m_fogBuffer,0u);
}
const std::shared_ptr<prosper::IBuffer> &CSceneComponent::GetRenderSettingsBuffer() const {return m_renderSettingsBuffer;}
pragma::RenderSettings &CSceneComponent::GetRenderSettings() {return m_renderSettings;}
const pragma::RenderSettings &CSceneComponent::GetRenderSettings() const {return const_cast<CSceneComponent*>(this)->GetRenderSettings();}
const std::shared_ptr<prosper::IBuffer> &CSceneComponent::GetCameraBuffer() const {return m_cameraBuffer;}
const std::shared_ptr<prosper::IBuffer> &CSceneComponent::GetViewCameraBuffer() const {return m_cameraViewBuffer;}
const std::shared_ptr<prosper::IDescriptorSetGroup> &CSceneComponent::GetCameraDescriptorSetGroup(prosper::PipelineBindPoint bindPoint) const
{
	switch(bindPoint)
	{
		case prosper::PipelineBindPoint::Graphics:
			return m_camDescSetGroupGraphics;
		case prosper::PipelineBindPoint::Compute:
			return m_camDescSetGroupCompute;
	}
	static std::shared_ptr<prosper::IDescriptorSetGroup> nptr = nullptr;
	return nptr;
}
const std::shared_ptr<prosper::IDescriptorSetGroup> &CSceneComponent::GetViewCameraDescriptorSetGroup() const {return m_camViewDescSetGroup;}
prosper::IDescriptorSet *CSceneComponent::GetCameraDescriptorSetGraphics() const {return m_camDescSetGroupGraphics->GetDescriptorSet();}
prosper::IDescriptorSet *CSceneComponent::GetCameraDescriptorSetCompute() const {return m_camDescSetGroupCompute->GetDescriptorSet();}
prosper::IDescriptorSet *CSceneComponent::GetViewCameraDescriptorSet() const {return m_camViewDescSetGroup->GetDescriptorSet();}
const std::shared_ptr<prosper::IDescriptorSetGroup> &CSceneComponent::GetFogDescriptorSetGroup() const {return m_fogDescSetGroup;}

void CSceneComponent::BuildRenderQueues(const util::DrawSceneInfo &drawSceneInfo)
{
	pragma::CEDrawSceneInfo evData {drawSceneInfo};
	InvokeEventCallbacks(pragma::CSceneComponent::EVENT_ON_BUILD_RENDER_QUEUES,evData);
	GetSceneRenderDesc().BuildRenderQueues(drawSceneInfo);

	// Start building the render queues for the light sources
	// that create shadows and were previously visible.
	// At this point we don't actually know if they're still visible,
	// but it's very likely.
	for(auto &hLight : m_previouslyVisibleShadowedLights)
	{
		if(hLight.expired())
			continue;
		auto *shadowC = hLight->GetShadowComponent();
		if(shadowC == nullptr)
			continue;
		shadowC->GetRenderer().BuildRenderQueues(drawSceneInfo);
	}
}

WorldEnvironment *CSceneComponent::GetWorldEnvironment() const {return m_worldEnvironment.get();}
void CSceneComponent::SetWorldEnvironment(WorldEnvironment &env)
{
	ClearWorldEnvironment();

	m_worldEnvironment = env.shared_from_this();
	m_envCallbacks.push_back(m_worldEnvironment->GetAmbientColorProperty()->AddCallback([this](std::reference_wrapper<const Vector4> oldColor,std::reference_wrapper<const Vector4> newColor) {
		m_renderSettings.ambientColor = newColor.get();
	}));
	m_envCallbacks.push_back(m_worldEnvironment->GetShaderQualityProperty()->AddCallback([this](std::reference_wrapper<const int32_t> oldVal,std::reference_wrapper<const int32_t> newVal) {
		m_renderSettings.shaderQuality = newVal.get();
	}));
	m_envCallbacks.push_back(m_worldEnvironment->GetShadowResolutionProperty()->AddCallback([this](std::reference_wrapper<const uint32_t> oldVal,std::reference_wrapper<const uint32_t> newVal) {
		Vector2 shadowRatio{
			1.f /static_cast<float>(newVal.get()),
			1.f /static_cast<float>(newVal.get())
		};
		m_renderSettings.shadowRatioX = shadowRatio.x;
		m_renderSettings.shadowRatioY = shadowRatio.y;
	}));
	m_envCallbacks.push_back(m_worldEnvironment->GetUnlitProperty()->AddCallback([this](std::reference_wrapper<const bool> oldVal,std::reference_wrapper<const bool> newVal) {
		UpdateRenderSettings();
	}));

	// Fog
	auto &fog = m_worldEnvironment->GetFogSettings();
	m_envCallbacks.push_back(fog.GetColorProperty()->AddCallback([this](std::reference_wrapper<const Color> oldVal,std::reference_wrapper<const Color> newVal) {
		m_fogData.color = newVal.get().ToVector4();
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),color),m_fogData.color);
	}));
	m_envCallbacks.push_back(fog.GetStartProperty()->AddCallback([this](std::reference_wrapper<const float> oldVal,std::reference_wrapper<const float> newVal) {
		m_fogData.start = newVal.get();
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),start),m_fogData.start);
	}));
	m_envCallbacks.push_back(fog.GetEndProperty()->AddCallback([this](std::reference_wrapper<const float> oldVal,std::reference_wrapper<const float> newVal) {
		m_fogData.end = newVal.get();
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),end),m_fogData.end);
	}));
	m_envCallbacks.push_back(fog.GetMaxDensityProperty()->AddCallback([this](std::reference_wrapper<const float> oldVal,std::reference_wrapper<const float> newVal) {
		m_fogData.density = newVal.get();
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),density),m_fogData.density);
	}));
	m_envCallbacks.push_back(fog.GetTypeProperty()->AddCallback([this](std::reference_wrapper<const uint8_t> oldVal,std::reference_wrapper<const uint8_t> newVal) {
		m_fogData.type = static_cast<uint32_t>(newVal.get());
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),type),m_fogData.type);
	}));
	m_envCallbacks.push_back(fog.GetEnabledProperty()->AddCallback([this](std::reference_wrapper<const bool> oldVal,std::reference_wrapper<const bool> newVal) {
		m_fogData.flags = static_cast<uint32_t>(newVal.get());
		c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,offsetof(decltype(m_fogData),flags),m_fogData.flags);
	}));
	m_fogData.color = fog.GetColor().ToVector4();
	m_fogData.start = fog.GetStart();
	m_fogData.end = fog.GetEnd();
	m_fogData.density = fog.GetMaxDensity();
	m_fogData.type = umath::to_integral(fog.GetType());
	m_fogData.flags = fog.IsEnabled();
	c_engine->GetRenderContext().ScheduleRecordUpdateBuffer(m_fogBuffer,0ull,m_fogData);
}
void CSceneComponent::SetLightMap(pragma::CLightMapComponent &lightMapC)
{
	auto &renderSettings = GetRenderSettings();
	m_lightMap = lightMapC.GetHandle<pragma::CLightMapComponent>();
	auto &prop = lightMapC.GetLightMapExposureProperty();
	UpdateRenderSettings();
	UpdateRendererLightMap();
}
void CSceneComponent::UpdateRendererLightMap()
{
	if(m_renderer.expired() || m_lightMap.expired())
		return;
	auto &texLightMap = m_lightMap->GetLightMap();
	if(texLightMap == nullptr)
		return;
	// TODO: Not ideal to have this here; How to handle this in a better way?
	auto raster = m_renderer->GetEntity().GetComponent<pragma::CRasterizationRendererComponent>();
	if(raster.valid())
		raster->SetLightMap(*m_lightMap);
}
void CSceneComponent::UpdateRenderSettings()
{
	if(m_worldEnvironment == nullptr)
		return;
	auto &unlitProperty = m_worldEnvironment->GetUnlitProperty();
	auto flags = FRenderSetting::None;
	if(unlitProperty->GetValue() == true)
		flags |= FRenderSetting::Unlit;
	m_renderSettings.flags = umath::to_integral(flags);
	if(m_renderer.valid())
		m_renderer->UpdateRenderSettings();
}
void CSceneComponent::ClearWorldEnvironment()
{
	for(auto &hCb : m_envCallbacks)
	{
		if(hCb.IsValid())
			hCb.Remove();
	}
	m_envCallbacks.clear();
	if(m_cbFogCallback.IsValid() == true)
		m_cbFogCallback.Remove();
	m_worldEnvironment = nullptr;
}

/*Vulkan::Texture &CSceneComponent::ResolveRenderTexture(Vulkan::CommandBufferObject *cmdBuffer) {return const_cast<Vulkan::Texture&>(m_hdrInfo.texture->Resolve(cmdBuffer));}
Vulkan::Texture &CSceneComponent::ResolveDepthTexture(Vulkan::CommandBufferObject *cmdBuffer) {return const_cast<Vulkan::Texture&>(m_hdrInfo.prepass.textureDepth->Resolve(cmdBuffer));}
Vulkan::Texture &CSceneComponent::ResolveBloomTexture(Vulkan::CommandBufferObject *cmdBuffer) {return const_cast<Vulkan::Texture&>(m_hdrInfo.textureBloom->Resolve(cmdBuffer));}*/ // prosper TODO

void CSceneComponent::Resize(uint32_t width,uint32_t height,bool reload)
{
	if(m_renderer.expired() || (reload == false && width == GetWidth() && height == GetHeight()))
		return;
	ReloadRenderTarget(width,height);
}
void CSceneComponent::LinkWorldEnvironment(CSceneComponent &other)
{
	// T
	auto &worldEnv = *GetWorldEnvironment();
	auto &worldEnvOther = *other.GetWorldEnvironment();
	worldEnv.GetAmbientColorProperty()->Link(*worldEnvOther.GetAmbientColorProperty());
	worldEnv.GetShaderQualityProperty()->Link(*worldEnvOther.GetShaderQualityProperty());
	worldEnv.GetShadowResolutionProperty()->Link(*worldEnvOther.GetShadowResolutionProperty());
	worldEnv.GetUnlitProperty()->Link(*worldEnvOther.GetUnlitProperty());

	auto &fogSettings = worldEnv.GetFogSettings();
	auto &fogSettingsOther = worldEnvOther.GetFogSettings();
	fogSettings.GetColorProperty()->Link(*fogSettingsOther.GetColorProperty());
	fogSettings.GetEnabledProperty()->Link(*fogSettingsOther.GetEnabledProperty());
	fogSettings.GetEndProperty()->Link(*fogSettingsOther.GetEndProperty());
	fogSettings.GetMaxDensityProperty()->Link(*fogSettingsOther.GetMaxDensityProperty());
	fogSettings.GetStartProperty()->Link(*fogSettingsOther.GetStartProperty());
	fogSettings.GetTypeProperty()->Link(*fogSettingsOther.GetTypeProperty());
}

void CSceneComponent::SetRenderer(CRendererComponent *renderer)
{
	m_renderer = renderer ? renderer->GetHandle<CRendererComponent>() : util::WeakHandle<CRendererComponent>{};
	UpdateRenderSettings();
	UpdateRendererLightMap();
}
pragma::CRendererComponent *CSceneComponent::GetRenderer() {return m_renderer.get();}
const pragma::CRendererComponent *CSceneComponent::GetRenderer() const {return const_cast<CSceneComponent*>(this)->GetRenderer();}

SceneDebugMode CSceneComponent::GetDebugMode() const {return m_debugMode;}
void CSceneComponent::SetDebugMode(SceneDebugMode debugMode) {m_debugMode = debugMode;}

SceneRenderDesc &CSceneComponent::GetSceneRenderDesc() {return m_sceneRenderDesc;}
const SceneRenderDesc &CSceneComponent::GetSceneRenderDesc() const {return const_cast<CSceneComponent*>(this)->GetSceneRenderDesc();}

void CSceneComponent::SetParticleSystemColorFactor(const Vector4 &colorFactor) {m_particleSystemColorFactor = colorFactor;}
const Vector4 &CSceneComponent::GetParticleSystemColorFactor() const {return m_particleSystemColorFactor;}

bool CSceneComponent::IsValid() const {return umath::is_flag_set(m_stateFlags,StateFlags::ValidRenderer);}

CSceneComponent *CSceneComponent::GetParentScene()
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::HasParentScene) == false)
		return nullptr;
	return GetByIndex(GetSceneIndex());
}

CSceneComponent::SceneIndex CSceneComponent::GetSceneIndex() const {return m_sceneIndex;}

uint32_t CSceneComponent::GetWidth() const {return m_renderer.valid() ? m_renderer->GetWidth() : 0;}
uint32_t CSceneComponent::GetHeight() const {return m_renderer.valid() ? m_renderer->GetHeight() : 0;}

//const Vulkan::DescriptorSet &CSceneComponent::GetBloomGlowDescriptorSet() const {return m_descSetBloomGlow;} // prosper TODO

void CSceneComponent::ReloadRenderTarget(uint32_t width,uint32_t height)
{
	umath::set_flag(m_stateFlags,StateFlags::ValidRenderer,false);

	if(m_renderer.expired() || m_renderer->ReloadRenderTarget(*this,width,height) == false)
		return;
	
	umath::set_flag(m_stateFlags,StateFlags::ValidRenderer,true);
}

const util::WeakHandle<pragma::CCameraComponent> &CSceneComponent::GetActiveCamera() const {return const_cast<CSceneComponent*>(this)->GetActiveCamera();}
util::WeakHandle<pragma::CCameraComponent> &CSceneComponent::GetActiveCamera() {return m_camera;}
void CSceneComponent::SetActiveCamera(pragma::CCameraComponent &cam)
{
	m_camera = cam.GetHandle<pragma::CCameraComponent>();
	m_renderSettings.nearZ = cam.GetNearZ();
	m_renderSettings.farZ = cam.GetFarZ();

	BroadcastEvent(EVENT_ON_ACTIVE_CAMERA_CHANGED);
}
void CSceneComponent::SetActiveCamera()
{
	m_camera = {};

	BroadcastEvent(EVENT_ON_ACTIVE_CAMERA_CHANGED);
}

/////////////////

CEDrawSceneInfo::CEDrawSceneInfo(const util::DrawSceneInfo &drawSceneInfo)
	: drawSceneInfo{drawSceneInfo}
{}
void CEDrawSceneInfo::PushArguments(lua_State *l)
{
	Lua::Push<const util::DrawSceneInfo*>(l,&drawSceneInfo);
}

////////

void CScene::Initialize()
{
	CBaseEntity::Initialize();
	AddComponent<CSceneComponent>();
}
