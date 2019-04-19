#include "stdafx_client.h"
#include "pragma/clientstate/clientutil.h"
#include "pragma/console/c_cvar_global.h"
#include "pragma/game/c_game.h"
#include "pragma/console/cvar_global.h"
#include "pragma/entities/components/c_player_component.hpp"
#include <pragma/console/convars.h>
#include "pragma/console/c_cvar.h"
#include <pragma/lua/luacallback.h>
#include <pragma/networking/nwm_util.h>
#include "pragma/networking/wvclient.h"
#include <sharedutils/util.h>
#include <sharedutils/util_string.h>
#include "pragma/gui/widebugmipmaps.h"
#include "pragma/debug/c_debug_game_gui.h"
#include "pragma/gui/winetgraph.h"
#include "pragma/util/util_tga.hpp"
#include <wgui/wgui.h>
#include <cmaterialmanager.h>
#include <pragma/networking/netmessages.h>
#include <pragma/console/sh_cmd.h>
#include <pragma/console/util_cmd.hpp>
#include <pragma/audio/alsound_type.h>
#include <pragma/engine_info.hpp>
#include <debug/prosper_debug.hpp>
#include <prosper_util.hpp>
#include <buffers/prosper_buffer.hpp>
#include <image/prosper_render_target.hpp>
#include <prosper_memory_tracker.hpp>
#include <prosper_command_buffer.hpp>
#include <buffers/prosper_uniform_resizable_buffer.hpp>
#include <buffers/prosper_dynamic_resizable_buffer.hpp>
#include <pragma/entities/components/base_transform_component.hpp>
#include <wrappers/memory_block.h>

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

DLLCLIENT void CMD_entities_cl(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string>&)
{
	if(!state->IsGameActive())
		return;
	auto sortedEnts = util::cmd::get_sorted_entities(*c_game,pl);
	for(auto &pair : sortedEnts)
		Con::cout<<*pair.first<<Con::endl;
}

void CMD_thirdperson(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	if(pl == nullptr)
		return;
	auto *cstate = static_cast<ClientState*>(state);
	CHECK_CHEATS("thirdperson",cstate,);
	auto bThirdPerson = false;
	if(!argv.empty())
		bThirdPerson = (atoi(argv.front().c_str()) != 0) ? true : false;
	else
		bThirdPerson = (pl->GetObserverMode() != OBSERVERMODE::THIRDPERSON) ? true : false;
	pl->SetObserverMode((bThirdPerson == true) ? OBSERVERMODE::THIRDPERSON : OBSERVERMODE::FIRSTPERSON);
}

DLLCLIENT void CMD_setpos(NetworkState *state,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	if(argv.size() < 3)
		return;
	ClientState *cstate = static_cast<ClientState*>(state);
	CHECK_CHEATS("setpos",cstate,);
	Vector3 pos(atof(argv[0].c_str()),atof(argv[1].c_str()),atof(argv[2].c_str()));
	NetPacket p;
	nwm::write_vector(p,pos);
	cstate->SendPacketTCP("cmd_setpos",p);
}

DLLCLIENT void CMD_getpos(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string>&)
{
	if(!state->IsGameActive())
		return;
	CGame *game = static_cast<CGame*>(state->GetGameState());
	if(pl == NULL)
	{
		Con::cout<<"0 0 0"<<Con::endl;
		return;
	}
	auto *cPl = game->GetLocalPlayer();
	auto pTrComponent = cPl->GetEntity().GetTransformComponent();
	if(pTrComponent.expired())
	{
		Con::cout<<"0 0 0"<<Con::endl;
		return;
	}
	auto &pos = pTrComponent->GetPosition();
	Con::cout<<pos.x<<" "<<pos.y<<" "<<pos.z<<Con::endl;
}

DLLCLIENT void CMD_getcampos(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	if(!state->IsGameActive())
		return;
	auto *game = static_cast<CGame*>(state->GetGameState());
	auto *pCam = game->GetRenderCamera();
	if(pCam == nullptr)
		pCam = &game->GetSceneCamera();
	auto &pos = pCam->GetPos();
	Con::cout<<pos.x<<" "<<pos.y<<" "<<pos.z<<Con::endl;
}

DLLCLIENT void CMD_setang(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	if(argv.size() < 3)
		return;
	ClientState *cstate = static_cast<ClientState*>(state);
	if(!cstate->IsGameActive())
		return;
	if(pl == NULL)
		return;
	CHECK_CHEATS("setang",cstate,);
	auto charComponent = pl->GetEntity().GetCharacterComponent();
	if(charComponent.expired())
		return;
	EulerAngles ang(util::to_float(argv[0]),util::to_float(argv[1]),util::to_float(argv[2]));
	charComponent->SetViewAngles(ang);
}

DLLCLIENT void CMD_getang(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string>&)
{
	if(!state->IsGameActive())
		return;
	if(pl == NULL)
	{
		Con::cout<<"0 0 0"<<Con::endl;
		return;
	}
	auto charComponent = pl->GetEntity().GetCharacterComponent();
	if(charComponent.expired())
		return;
	EulerAngles ang = charComponent->GetViewAngles();
	Con::cout<<ang.p<<" "<<ang.y<<" "<<ang.r<<Con::endl;
}

DLLCLIENT void CMD_getcamang(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string>&)
{
	if(!state->IsGameActive())
		return;
	auto *game = static_cast<CGame*>(state->GetGameState());
	auto *pCam = game->GetRenderCamera();
	if(pCam == nullptr)
		pCam = &game->GetSceneCamera();
	auto ang = EulerAngles{pCam->GetRotation()};
	Con::cout<<ang.p<<" "<<ang.y<<" "<<ang.r<<Con::endl;
}

DLLCLIENT void CMD_sound_play(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	if(argv.empty())
		return;
	if(client->PrecacheSound(argv[0]) == 0)
		return;
	client->PlaySound(argv[0],ALSoundType::GUI,ALCreateFlags::None);
}

DLLCLIENT void CMD_sound_stop(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	client->StopSounds();
}

DLLCLIENT void CMD_status_cl(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	auto &players = pragma::CPlayerComponent::GetAll();
	auto *cl = client->GetClient();
	if(cl == nullptr)
	{
		Con::cwar<<"WARNING: Not connected to a server!"<<Con::endl;
		return;
	}
	Con::cout<<"hostname:\t"<<"Unknown"<<Con::endl;
	Con::cout<<"udp/ip:\t\t"<<cl->GetIP()<<Con::endl;
	Con::cout<<"map:\t\t"<<"Unknown"<<Con::endl;
	Con::cout<<"players:\t"<<players.size()<<" ("<<0<<" max)"<<Con::endl<<Con::endl;
	Con::cout<<"#  userid\tname    \tconnected\tping";
	Con::cout<<Con::endl;
	auto i = 0u;
	for(auto *plComponent : players)
	{
		Con::cout<<"# \t"<<i<<"\t"<<"\""<<plComponent->GetPlayerName()<<"\""<<"\t"<<FormatTime(plComponent->TimeConnected())<<"     \t";
		if(plComponent->IsLocalPlayer() == true)
			Con::cout<<cl->GetLatency();
		else
			Con::cout<<"?";
		Con::cout<<Con::endl;
		++i;
	}
}

#ifdef _DEBUG
void CMD_cl_dump_sounds(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	auto &sounds = client->GetSounds();
	for(auto &sndInfo : sounds)
	{
		auto &snd = sndInfo.sound;
		if(sndInfo.container == false)
		{
			Con::cout<<sndInfo.index<<": ";
			if(snd == nullptr)
				Con::cout<<"NULL";
			else
			{
				auto *csnd = static_cast<CALSound*>(snd.get());
				auto src = csnd->GetSource();
				auto buf = csnd->GetBuffer();
				Con::cout<<"Buffer "<<buf<<" on source "<<src<<";";
				auto state = csnd->GetState();
				Con::cout<<" State: ";
				switch(state)
				{
					case AL_INITIAL:
						Con::cout<<"Initial";
						break;
					case AL_PLAYING:
						Con::cout<<"Playing";
						break;
					case AL_PAUSED:
						Con::cout<<"Paused";
						break;
					case AL_STOPPED:
						Con::cout<<"Stopped";
						break;
					default:
						Con::cout<<"Unknown";
						break;
				}
				Con::cout<<"; Source: ";
				std::string name;
				if(client->GetSoundName(buf,name) == true)
					Con::cout<<name;
				else
					Con::cout<<"Unknown";
			}
			Con::cout<<Con::endl;
		}
	}
}

void CMD_cl_dump_netmessages(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	auto *map = GetClientMessageMap();
	std::unordered_map<std::string,unsigned int> *netmessages;
	map->GetNetMessages(&netmessages);
	if(!argv.empty())
	{
		auto id = atoi(argv.front().c_str());
		for(auto it=netmessages->begin();it!=netmessages->end();++it)
		{
			if(it->second == id)
			{
				Con::cout<<"Message Identifier: "<<it->first<<Con::endl;
				return;
			}
		}
		Con::cout<<"No message with id "<<id<<" found!"<<Con::endl;
		return;
	}
	for(auto it=netmessages->begin();it!=netmessages->end();++it)
		Con::cout<<it->first<<" = "<<it->second<<Con::endl;
}
#endif

void CMD_screenshot(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	auto *game = client->GetGameState();
	if(game == nullptr)
		return;
	/*int32_t mode = 0;
	if(!argv.empty())
		mode = atoi(argv.front().c_str());*/
	std::string map;
	if(game == nullptr)
		map = engine_info::get_identifier();
	else
		map = game->GetMapName();
	std::string path;
	int i = 1;
	do
	{
		path = "screenshots\\";
		path += map;
		path += ustring::fill_zeroes(std::to_string(i),4);
		path += ".tga";
		i++;
	}
	while(FileManager::Exists(path.c_str()/*,fsys::SearchFlags::Local*/));

	auto scene = game->GetScene();
	auto rt = (scene != nullptr) ? scene->GetHDRInfo().postHdrRenderTarget : nullptr;
	if(rt == nullptr)
		return;
	auto f = FileManager::OpenFile<VFilePtrReal>(path.c_str(),"wb");
	if(f == nullptr)
		return;
	c_engine->WaitIdle(); // Make sure rendering is complete

	uint32_t queueFamilyIndex;
	auto cmdBuffer = c_engine->AllocatePrimaryLevelCommandBuffer(Anvil::QueueFamilyType::UNIVERSAL,queueFamilyIndex);
	
	auto &img = rt->GetTexture()->GetImage();
	auto extents = img->GetExtents();
	prosper::util::ImageCreateInfo createInfo {};
	createInfo.memoryFeatures = prosper::util::MemoryFeatureFlags::GPUToCPU;
	createInfo.width = extents.width;
	createInfo.height = extents.height;
	createInfo.tiling = Anvil::ImageTiling::LINEAR;
	createInfo.format = Anvil::Format::R8G8B8A8_UNORM;
	createInfo.postCreateLayout = Anvil::ImageLayout::TRANSFER_DST_OPTIMAL;
	createInfo.usage = Anvil::ImageUsageFlagBits::TRANSFER_DST_BIT;
	auto imgDst = prosper::util::create_image(c_engine->GetDevice(),createInfo);

	// TODO: Check if image formats are compatible (https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-formats-compatibility)
	// before issuing the copy command
	cmdBuffer->StartRecording();
		prosper::util::record_image_barrier(**cmdBuffer,**img,Anvil::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,Anvil::ImageLayout::TRANSFER_SRC_OPTIMAL);
		prosper::util::record_copy_image(**cmdBuffer,{},**img,**imgDst);
		prosper::util::record_image_barrier(**cmdBuffer,**img,Anvil::ImageLayout::TRANSFER_SRC_OPTIMAL,Anvil::ImageLayout::COLOR_ATTACHMENT_OPTIMAL);
		// Note: Blit can't be used because some Nvidia GPUs don't support blitting for images with linear tiling
		//prosper::util::record_blit_image(**cmdBuffer,{},**img,**imgDst);
	cmdBuffer->StopRecording();
	c_engine->SubmitCommandBuffer(*cmdBuffer,true);

	auto format = createInfo.format;
	auto bSwapped = false;
	if(format == Anvil::Format::B8G8R8A8_UNORM)
	{
		format = Anvil::Format::R8G8B8A8_UNORM;
		bSwapped = true;
	}
	else if(format == Anvil::Format::B8G8R8_UNORM)
	{
		format = Anvil::Format::R8G8B8_UNORM;
		bSwapped = true;
	}

	void *data;
	auto byteSize = prosper::util::get_byte_size(format);
	auto numBytes = createInfo.width *createInfo.height *byteSize;
	imgDst->GetAnvilImage().get_memory_block()->map(0ull,numBytes,&data);
	Anvil::SubresourceLayout layout;
	imgDst->GetAnvilImage().get_aspect_subresource_layout(Anvil::ImageAspectFlagBits::COLOR_BIT,0u,0u,&layout);
	auto offset = layout.offset;
	auto rowPitch = layout.row_pitch;
	std::vector<unsigned char> pixels(numBytes);
	size_t pos = 0;
	auto *ptr = static_cast<const char*>(data);
	for(auto y=decltype(createInfo.height){0u};y<createInfo.height;++y)
	{
		ptr = static_cast<const char*>(data) +offset +y *rowPitch;
		auto *row = ptr;
		if(format == Anvil::Format::R8G8B8A8_UNORM)
		{
			for(auto x=decltype(createInfo.width){0};x<createInfo.width;++x)
			{
				auto *px = static_cast<const unsigned char*>(static_cast<const void*>(row));
				pixels[pos] = (bSwapped == false) ? px[0] : px[2];
				pixels[pos +1] = px[1];
				pixels[pos +2] = (bSwapped == false) ? px[2] : px[0];
				pos += 3;
				row += byteSize;
			}
		}
		else if(format == Anvil::Format::R8G8B8_UNORM)
		{
			for(auto x=decltype(createInfo.width){0};x<createInfo.width;++x)
			{
				auto *px = static_cast<const unsigned char*>(static_cast<const void*>(row));
				pixels[pos] = (bSwapped == false) ? px[0] : px[2];
				pixels[pos +1] = px[1];
				pixels[pos +2] = (bSwapped == false) ? px[2] : px[0];
				pos += 3;
				row += byteSize;
			}
		}
		else if(format == Anvil::Format::R8_UNORM)
		{
			for(auto x=decltype(createInfo.width){0};x<createInfo.width;++x)
			{
				auto *px = static_cast<const unsigned char*>(static_cast<const void*>(row));
				pixels[pos] = px[0];
				pixels[pos +1] = px[0];
				pixels[pos +2] = px[0];
				pos += 3;
				row += byteSize;
			}
		}
		else if(format == Anvil::Format::R16_UNORM)
		{
			for(auto x=decltype(createInfo.width){0};x<createInfo.width;++x)
			{
				auto *px = static_cast<const unsigned char*>(static_cast<const void*>(row));
				pixels[pos] = px[0];
				pixels[pos +1] = px[0];
				pixels[pos +2] = px[0];
				pos += 3;
				row += byteSize;
			}
		}
		else if(format == Anvil::Format::D16_UNORM)
		{
			for(auto x=decltype(createInfo.width){0};x<createInfo.width;++x)
			{
				auto val = 0.f;
				auto *px = static_cast<const unsigned char*>(static_cast<const void*>(row));
				memcpy(&val,px,byteSize);
				val = umath::min(val *100.f,255.f);
				pixels[pos] = static_cast<unsigned char>(val);
				pixels[pos +1] = static_cast<unsigned char>(val);
				pixels[pos +2] = static_cast<unsigned char>(val);
				pos += 3;
				row += byteSize;
			}
		}
	}
	util::tga::write_tga(f,createInfo.width,createInfo.height,pixels);
}

DLLCLIENT void CMD_shader_reload(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	if(argv.empty())
	{
		auto &shaderManager = c_engine->GetShaderManager();
		auto &shaders = shaderManager.GetShaders();
		for(auto &pair : shaders)
		{
			Con::cout<<"Reloading shader '"<<pair.first<<"'..."<<Con::endl;
			c_engine->ReloadShader(pair.first);
		}
		Con::cout<<"All shaders have been reloaded!"<<Con::endl;
		return;
	}
	c_engine->ReloadShader(argv.front());
}

void CMD_shader_list(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	auto &shaderManager = c_engine->GetShaderManager();
	std::vector<std::shared_ptr<prosper::Shader>> shaderList;
	auto shaders = shaderManager.GetShaders();
	shaderList.reserve(shaders.size());
	for(auto &pair : shaders)
		shaderList.push_back(pair.second);
	std::sort(shaderList.begin(),shaderList.end(),[](const std::shared_ptr<prosper::Shader> &a,const std::shared_ptr<prosper::Shader> &b) {
		return (a->GetIdentifier() < b->GetIdentifier()) ? true : false;
	});
	for(auto &shader : shaderList)
	{
		auto &id = shader->GetIdentifier();
		Con::cout<<id;
		if(shader->IsComputeShader())
			Con::cout<<" (Compute)";
		else if(shader->IsGraphicsShader())
			Con::cout<<" (Graphics)";
		else
			Con::cout<<" (Unknown)";
		auto &shaderSources = shader->GetSourceFilePaths();
		for(auto &src : shaderSources)
			Con::cout<<" ("<<src<<")";
		Con::cout<<Con::endl;
	}
}

DLLCLIENT void CMD_flashlight_toggle(NetworkState*,pragma::BasePlayerComponent *pl,std::vector<std::string>&)
{
	CGame *game = client->GetGameState();
	if(game == NULL)
		return;
	if(pl == NULL)
		return;
	pl->ToggleFlashlight();
}

void CMD_debug_ai_schedule_print(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	CHECK_CHEATS("debug_ai_schedule_print",state,);
	if(c_game == nullptr || pl == nullptr)
		return;
	auto charComponent = pl->GetEntity().GetCharacterComponent();
	if(charComponent.expired())
		return;
	auto ents = command::find_target_entity(state,*charComponent,argv);
	BaseEntity *npc = nullptr;
	for(auto *ent : ents)
	{
		if(ent->IsNPC() == false)
			continue;
		npc = ent;
		break;
	}
	if(npc == nullptr)
	{
		Con::cwar<<"WARNING: No valid NPC target found!"<<Con::endl;
		return;
	}
	Con::cout<<"Querying schedule data for NPC "<<*npc<<"..."<<Con::endl;
	NetPacket p;
	nwm::write_entity(p,npc);
	client->SendPacketTCP("debug_ai_schedule_print",p);
}

DLLCLIENT void CMD_reloadmaterial(NetworkState *state,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	CHECK_CHEATS("reloadmaterial",state,);
	if(argv.empty())
		return;
	Con::cout<<"Reloading '"<<argv[0]<<"'..."<<Con::endl;
	client->LoadMaterial(argv[0].c_str(),true,true);
}

DLLCLIENT void CMD_reloadmaterials(NetworkState *state,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	CHECK_CHEATS("reloadmaterials",state,);

}

void Console::commands::cl_list(NetworkState *state,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	auto &convars = state->GetConVars();
	std::vector<std::string> cvars(convars.size());
	size_t idx = 0;
	for(auto &pair : convars)
	{
		cvars[idx] = pair.first;
		idx++;
	}
	std::sort(cvars.begin(),cvars.end());
	std::vector<std::string>::iterator it;
	for(it=cvars.begin();it!=cvars.end();it++)
		Con::cout<<*it<<Con::endl;
}

void Console::commands::cl_find(NetworkState *state,pragma::BasePlayerComponent*,std::vector<std::string> &argv)
{
	if(argv.empty())
	{
		Con::cwar<<"WARNING: No argument given!"<<Con::endl;
		return;
	}
	auto similar = state->FindSimilarConVars(argv.front());
	if(similar.empty())
	{
		Con::cout<<"No potential candidates found!"<<Con::endl;
		return;
	}
	Con::cout<<"Found "<<similar.size()<<" potential candidates:"<<Con::endl;
	for(auto &name : similar)
		Con::cout<<"- "<<name<<Con::endl;
}

void CMD_fps(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	Con::cout<<"FPS: "<<c_engine->GetFPS()<<Con::endl<<"Frame Time: "<<c_engine->GetFrameTime()<<"ms"<<Con::endl;
}

void Console::commands::vk_dump_limits(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_limits(*c_engine,"vk_limits.txt");
}
void Console::commands::vk_dump_features(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_features(*c_engine,"vk_features.txt");
}
void Console::commands::vk_dump_format_properties(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_format_properties(*c_engine,"vk_format_properties.txt");
}
void Console::commands::vk_dump_image_format_properties(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_image_format_properties(*c_engine,"vk_image_format_properties.txt");
}
void Console::commands::vk_dump_layers(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_layers(*c_engine,"vk_layers.txt");
}
void Console::commands::vk_dump_extensions(NetworkState*,pragma::BasePlayerComponent*,std::vector<std::string>&)
{
	prosper::debug::dump_extensions(*c_engine,"vk_extensions.txt");
}
/*static void print_memory_stats(std::stringstream &ss,Vulkan::MemoryManager::StatInfo &info)
{
	ss<<"Number of allocations: "<<info.allocationCount<<"\n";
	ss<<"Allocation size bounds (avg,min,max): "<<
		util::get_pretty_bytes(info.allocationSizeAvg)<<","<<
		util::get_pretty_bytes(info.allocationSizeMin)<<","<<
		util::get_pretty_bytes(info.allocationSizeMax)<<",\n";
	ss<<"Block count: "<<info.blockCount<<"\n";
	ss<<"Used data: "<<util::get_pretty_bytes(info.usedBytes)<<"\n";
	ss<<"Unused data: "<<util::get_pretty_bytes(info.unusedBytes)<<"\n";
	ss<<"Unused range (count,avg,min,max): "<<info.unusedRangeCount<<","<<
		util::get_pretty_bytes(info.unusedRangeSizeAvg)<<","<<
		util::get_pretty_bytes(info.unusedRangeSizeMin)<<","<<
		util::get_pretty_bytes(info.unusedRangeSizeMax)<<"\n";
	ss<<"\n";
}*/ // prosper TODO
void Console::commands::vk_dump_memory_stats(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	/*auto &context = c_engine->GetRenderContext();
	auto &memoryMan = context.GetMemoryManager();
	auto stats = memoryMan.GetStatistics();
	auto f = FileManager::OpenFile<VFilePtrReal>("vk_memory_stats.txt","w");
	if(f == nullptr)
		return;
	std::stringstream ss;
	ss<<"Total memory usage:\n";
	print_memory_stats(ss,stats.total);
	ss<<"\n";
	auto idx = 0u;
	for(auto &heapStats : stats.memoryHeap)
	{
		ss<<"Heap "<<idx++<<" memory usage:\n";
		print_memory_stats(ss,heapStats);
	}
	ss<<"\n";
	idx = 0u;
	for(auto &heapStats : stats.memoryHeap)
	{
		ss<<"Type "<<idx++<<" memory usage:\n";
		print_memory_stats(ss,heapStats);
	}
	f->WriteString(ss.str());*/ // prosper TODO
}
void Console::commands::vk_print_memory_stats(NetworkState *state,pragma::BasePlayerComponent *pl,std::vector<std::string> &argv)
{
	auto &dev = c_engine->GetDevice();
	auto &memProps = dev.get_physical_device_memory_properties();
	auto allocatedDeviceLocalSize = 0ull;
	auto &memTracker = prosper::MemoryTracker::GetInstance();
	std::vector<uint32_t> deviceLocalTypes = {};
	deviceLocalTypes.reserve(memProps.types.size());
	for(auto i=decltype(memProps.types.size()){0u};i<memProps.types.size();++i)
	{
		auto &type = memProps.types.at(i);
		if(type.heap_ptr == nullptr || (type.flags &Anvil::MemoryPropertyFlagBits::DEVICE_LOCAL_BIT) == Anvil::MemoryPropertyFlagBits::NONE)
			continue;
		if(deviceLocalTypes.empty() == false)
			assert(type.heap_trp->size() == memProps.types.at(deviceLocalTypes.front()).heap_ptr->size());
		deviceLocalTypes.push_back(i);
		auto allocatedSize = 0ull;
		auto totalSize = 0ull;
		memTracker.GetMemoryStats(*c_engine,i,allocatedSize,totalSize);
		allocatedDeviceLocalSize += allocatedSize;
	}
	if(deviceLocalTypes.empty())
	{
		Con::cwar<<"WARNING: No device local memory types found!"<<Con::endl;
		return;
	}
	std::stringstream ss;
	auto totalMemory = memProps.types.at(deviceLocalTypes.front()).heap_ptr->size;
	ss<<"Total Available GPU Memory: "<<util::get_pretty_bytes(totalMemory)<<"\n";
	ss<<"Memory in use: "<<util::get_pretty_bytes(allocatedDeviceLocalSize)<<" ("<<umath::round(allocatedDeviceLocalSize /static_cast<double>(totalMemory) *100.0,2)<<"%)\n";
	ss<<"Memory usage by resource type:\n";
	const std::unordered_map<prosper::MemoryTracker::Resource::TypeFlags,std::string> types = {
		{prosper::MemoryTracker::Resource::TypeFlags::StandAloneBufferBit,"Dedicated buffers"},
		{prosper::MemoryTracker::Resource::TypeFlags::UniformBufferBit,"Uniform resizable buffers"},
		{prosper::MemoryTracker::Resource::TypeFlags::DynamicBufferBit,"Dynamic resizable buffers"},
		{prosper::MemoryTracker::Resource::TypeFlags::ImageBit,"Images"}
	};
	for(auto &pair : types)
	{
		auto allocatedSizeOfType = 0ull;
		std::vector<const prosper::MemoryTracker::Resource*> resources {};
		for(auto idx : deviceLocalTypes)
		{
			auto allocatedSize = 0ull;
			auto totalSize = 0ull;
			memTracker.GetMemoryStats(*c_engine,idx,allocatedSize,totalSize,pair.first);
			allocatedSizeOfType += allocatedSize;

			memTracker.GetResources(idx,resources,pair.first);
		}
		std::vector<uint64_t> resourceSizes {};
		resourceSizes.reserve(resources.size());
		std::vector<size_t> sortedIndices = {};
		auto i = 0ull;
		for(auto *res : resources)
		{
			sortedIndices.push_back(i++);
			auto numMemoryBlocks = res->GetMemoryBlockCount();
			auto size = 0ull;
			for(auto i=decltype(numMemoryBlocks){0u};i<numMemoryBlocks;++i)
			{
				auto *memBlock = res->GetMemoryBlock(i);
				if(memBlock == nullptr)
					continue;
				size += memBlock->get_create_info_ptr()->get_size();
			}
			resourceSizes.push_back(size);
		}
		ss<<"\t"<<pair.second<<": "<<util::get_pretty_bytes(allocatedSizeOfType)<<" ("<<umath::round(allocatedSizeOfType /static_cast<double>(allocatedDeviceLocalSize) *100.0,2)<<"%)\n";
		ss<<"\tNumber of Resources: "<<resources.size()<<"\n";
		std::sort(sortedIndices.begin(),sortedIndices.end(),[&resourceSizes](const size_t idx0,const size_t idx1) {
			return resourceSizes.at(idx0) > resourceSizes.at(idx1);
		});
		for(auto idx : sortedIndices)
		{
			auto res = resources.at(idx);
			auto size = resourceSizes.at(idx);
			ss<<"\t\t";
			if((res->typeFlags &prosper::MemoryTracker::Resource::TypeFlags::StandAloneBufferBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
				ss<<"StandAloneBuffer: "<<static_cast<prosper::Buffer*>(res->resource)->GetDebugName();
			else if((res->typeFlags &prosper::MemoryTracker::Resource::TypeFlags::UniformBufferBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
				ss<<"UniformResizableBuffer: "<<static_cast<prosper::UniformResizableBuffer*>(res->resource)->GetDebugName();
			else if((res->typeFlags &prosper::MemoryTracker::Resource::TypeFlags::DynamicBufferBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
				ss<<"DynamicResizableBuffer: "<<static_cast<prosper::DynamicResizableBuffer*>(res->resource)->GetDebugName();
			else if((res->typeFlags &prosper::MemoryTracker::Resource::TypeFlags::ImageBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
				ss<<"Image: "<<static_cast<prosper::Image*>(res->resource)->GetDebugName();
			ss<<" "<<util::get_pretty_bytes(size)<<" ("<<umath::round(size /static_cast<double>(totalMemory) *100.0,2)<<"%)\n";
		}
		ss<<"\n";
	}

	auto &resources = prosper::MemoryTracker::GetInstance().GetResources();
	std::vector<size_t> sortedResourceIndices = {};
	sortedResourceIndices.reserve(resources.size());
	for(auto i=decltype(sortedResourceIndices.size()){0u};i<resources.size();++i)
	{
		if((resources.at(i).typeFlags &prosper::MemoryTracker::Resource::TypeFlags::BufferBit) == prosper::MemoryTracker::Resource::TypeFlags::None)
			continue;
		sortedResourceIndices.push_back(i);
	}
	std::sort(sortedResourceIndices.begin(),sortedResourceIndices.end(),[&resources](size_t idx0,size_t idx1) {
		return static_cast<prosper::Buffer*>(resources.at(idx0).resource)->GetSize() > static_cast<prosper::Buffer*>(resources.at(idx1).resource)->GetSize();
	});

	ss<<"Uniform resizable buffers:\n";
	for(auto idx : sortedResourceIndices)
	{
		auto &res = resources.at(idx);
		if((res.typeFlags &prosper::MemoryTracker::Resource::TypeFlags::UniformBufferBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
		{
			auto &uniBuf = *static_cast<prosper::UniformResizableBuffer*>(res.resource);
			ss<<uniBuf.GetDebugName()<<"\n";
			auto &allocatedSubBuffers = uniBuf.GetAllocatedSubBuffers();
			auto instanceCount = std::count_if(allocatedSubBuffers.begin(),allocatedSubBuffers.end(),[](const prosper::Buffer *buffer) {
				return buffer != nullptr;
			});
			auto assignedMemory = uniBuf.GetAssignedMemory();
			auto totalInstanceCount = uniBuf.GetTotalInstanceCount();
			ss<<"\tInstances in use: "<<instanceCount<<" / "<<totalInstanceCount<<" ("<<umath::round(instanceCount /static_cast<double>(totalInstanceCount) *100.0,2)<<"%)\n";
			ss<<"\tMemory in use: "<<util::get_pretty_bytes(assignedMemory)<<" / "<<util::get_pretty_bytes(uniBuf.GetSize())<<" ("<<umath::round(assignedMemory /static_cast<double>(uniBuf.GetSize()) *100.0,2)<<"%)\n";
		}
	}

	ss<<"\nDynamic resizable buffers:\n";
	for(auto &res : prosper::MemoryTracker::GetInstance().GetResources())
	{
		if((res.typeFlags &prosper::MemoryTracker::Resource::TypeFlags::DynamicBufferBit) != prosper::MemoryTracker::Resource::TypeFlags::None)
		{
			auto &dynBuf = *static_cast<prosper::DynamicResizableBuffer*>(res.resource);
			ss<<dynBuf.GetDebugName()<<"\n";
			auto &allocatedSubBuffers = dynBuf.GetAllocatedSubBuffers();
			auto instanceCount = std::count_if(allocatedSubBuffers.begin(),allocatedSubBuffers.end(),[](const prosper::Buffer *buffer) {
				return buffer != nullptr;
			});
			auto szFree = dynBuf.GetFreeSize();
			auto assignedMemory = dynBuf.GetSize() -szFree;
			ss<<"\tInstances: "<<instanceCount<<"\n";
			ss<<"\tMemory in use: "<<util::get_pretty_bytes(assignedMemory)<<" / "<<util::get_pretty_bytes(dynBuf.GetSize())<<" ("<<umath::round(assignedMemory /static_cast<double>(dynBuf.GetSize()) *100.0,2)<<"%)\n";
			ss<<"\tFragmentation: "<<dynBuf.GetFragmentationPercent()<<"\n";
		}
	}
	std::cout<<ss.str()<<std::endl;

	/*auto &context = c_engine->GetRenderContext();
	auto &memoryMan = context.GetMemoryManager();
	auto stats = memoryMan.GetStatistics();
	if(argv.empty())
	{
		Con::cout<<"Memory usage:"<<Con::endl;
		std::stringstream ss;
		print_memory_stats(ss,stats.total);
		Con::cout<<ss.str();
		Con::flush();
	}
	else
	{
		if(argv.size() == 1)
		{
			Con::cwar<<"WARNING: Not enough arguments given!"<<Con::endl;
			return;
		}
		auto id = util::to_int(argv.at(1));
		if(argv.front() == "heap")
		{
			if(id >= stats.memoryHeap.size())
			{
				Con::cwar<<"WARNING: Second argument has to be in the range [0,"<<stats.memoryHeap.size()<<"]!"<<Con::endl;
				return;
			}
			Con::cout<<"Memory usage for heap "<<id<<":"<<Con::endl;
			std::stringstream ss;
			print_memory_stats(ss,stats.memoryHeap.at(id));
			Con::cout<<ss.str();
			Con::flush();
		}
		else if(argv.front() == "type")
		{
			if(id >= stats.memoryType.size())
			{
				Con::cwar<<"WARNING: Second argument has to be in the range [0,"<<stats.memoryType.size()<<"]!"<<Con::endl;
				return;
			}
			Con::cout<<"Memory usage for type "<<id<<":"<<Con::endl;
			std::stringstream ss;
			print_memory_stats(ss,stats.memoryType.at(id));
			Con::cout<<ss.str();
			Con::flush();
		}
	}*/ // prosper TODO
}

static void cvar_net_graph(bool val)
{
	static std::unique_ptr<DebugGameGUI> dbg = nullptr;
	if(dbg == nullptr)
	{
		if(val == false)
			return;
		dbg = std::make_unique<DebugGameGUI>([]() {
			auto &wgui = WGUI::GetInstance();
			auto sz = wgui.GetContext().GetWindow().GetSize();
			auto el = wgui.Create<WINetGraph>();
			el->SetSize(540,180);
			el->SetPos(sz.x -el->GetWidth(),0);
			return el->GetHandle();
		});
		return;
	}
	else if(val == true)
		return;
	dbg = nullptr;
}
REGISTER_CONVAR_CALLBACK_CL(net_graph,[](NetworkState*,ConVar*,bool,bool val) {
	cvar_net_graph(val);
})
