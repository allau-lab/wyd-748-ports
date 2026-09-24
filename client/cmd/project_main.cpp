// Candidato Linux: Fase H+ — render exclusivo DXVK Native (fallback SPIR-V).

#include "wayland_window.h"
#include "asset_paths.h"
#include "vulkan_textured.h"
#include "vulkan_mesh.h"
#include "wyt_decode.h"
#include "sn_loader.h"
#include "shader_dx9_catalog.h"
#include "dx9_shader_runtime.h"
#include "d3d9_dxvk_native.h"
#include "object_mask.h"
#include "audio_sdl.h"
#include "login_session.h"
#include "msa_loader.h"
#include "msh_loader.h"
#include "bone_skin.h"
#include "bone_ani_catalog.h"
#include "wys_decode.h"
#include "selchar_panel.h"
#include "field_map.h"
#include "field_view.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	const char* EnvOr(const char* key, const char* fallback)
	{
		const char* v = std::getenv(key);
		return (v && v[0]) ? v : fallback;
	}

	bool WantField2DOnly()
	{
		const char* v = std::getenv("WYD_FIELD_2D");
		return v && v[0] == '1';
	}

	// WYD_USE_DXVK_NATIVE=0 força SPIR-V; caso contrário tenta DXVK se a .so existir.
	bool WantDxvkNative()
	{
		const char* v = std::getenv("WYD_USE_DXVK_NATIVE");
		if (v && v[0] == '0')
			return false;
		if (v && v[0] == '1')
			return true;
		return WYD_D3D9NativeAvailable();
	}

	void RefreshTitle(SDL_Window* window, const WYDServerNameList& servers,
		const WYDLoginSession& login, bool inField, bool mode3d, bool dxvk)
	{
		char title[220];
		const char* phase = "offline";
		if (login.rejected)
			phase = "reject";
		else if (inField && mode3d)
			phase = "field-3d";
		else if (inField)
			phase = "field-map";
		else if (login.field_entered)
			phase = "field";
		else if (login.charlogin_sent)
			phase = "wait-114";
		else if (login.cnf_received)
			phase = "selchar";
		else if (login.login_sent)
			phase = "wait-cnf";
		else if (login.connected)
			phase = "connected";

		std::snprintf(title, sizeof(title), "WYD 7.48 | %s | %s | %s",
			servers.names[0], phase, dxvk ? "dxvk" : "vk");
		SDL_SetWindowTitle(window, title);
	}

	bool TrySelectSlot(WYDLoginSession& login, WYDSelCharPanel& panel, int slot)
	{
		if (slot < 0 || slot > 3 || login.charlogin_sent || login.field_entered)
			return false;
		if (!WYD_LoginSendCharacter(login, slot))
		{
			std::fprintf(stderr, "[project] %s\n", login.status.c_str());
			return false;
		}
		panel.selected = slot;
		panel.dirty = true;
		std::fprintf(stderr, "[project] %s\n", login.status.c_str());
		return true;
	}

	bool EnterMesh3D(WYDVulkanTextured& gpu, WYDVulkanMesh& vmesh,
		SDL_Window* window, unsigned w, unsigned h, const WYDMsaMesh& mesh)
	{
		if (!mesh.vertex_count)
			return false;
		WYD_VulkanTexturedDestroy(gpu);
		gpu = {};
		if (!WYD_VulkanMeshCreate(vmesh, window, w, h) ||
			!WYD_VulkanMeshUpload(vmesh, mesh))
		{
			WYD_VulkanMeshDestroy(vmesh);
			vmesh = {};
			return false;
		}
		return true;
	}

	bool EnterMap2D(WYDVulkanMesh& vmesh, WYDVulkanTextured& gpu,
		SDL_Window* window, unsigned w, unsigned h)
	{
		WYD_VulkanMeshDestroy(vmesh);
		vmesh = {};
		if (!WYD_VulkanTexturedCreate(gpu, window, w, h))
			return false;
		return true;
	}

	uint32_t StandinClearArgb(const WYDDx9ShaderRuntime& dx9rt, uint32_t frames)
	{
		const int si = WYD_Dx9RuntimeActiveStandin(dx9rt, frames / 45);
		float r = 0.45f, g = 0.75f, b = 0.95f;
		if (si >= 0 && si < static_cast<int>(dx9rt.entries.size()))
		{
			r = 0.35f + 0.08f * static_cast<float>(si % 5);
			g = 0.55f + 0.06f * static_cast<float>((si + 2) % 5);
			b = 0.70f + 0.05f * static_cast<float>((si + 4) % 5);
		}
		const auto ch = [](float v) -> uint32_t {
			if (v < 0.f) v = 0.f;
			if (v > 1.f) v = 1.f;
			return static_cast<uint32_t>(v * 255.f + 0.5f);
		};
		return 0xFF000000u | (ch(r) << 16) | (ch(g) << 8) | ch(b);
	}

	struct FieldMeshInst
	{
		WYDD3D9NativeMesh mesh {};
		float ox = 0.f, oy = 0.f, oz = 0.f;
		float scale = 1.f;
		float spinMul = 1.f;
	};

	struct CharPart
	{
		WYDMshMesh msh {};
		WYDD3D9NativeMesh mesh {};
		bool ready = false;
	};

	struct CharSkinBundle
	{
		bool ready = false;
		int look = 102;
		int part_count = 0;
		WYDBonHierarchy bon {};
		WYDAniClip ani {};
		CharPart parts[8] {};
		float center[3] {};
		float radius = 1.f;
	};

	void DestroyCharSkin(CharSkinBundle& out)
	{
		for (int i = 0; i < out.part_count; ++i)
		{
			if (out.parts[i].ready)
				WYD_D3D9NativeMeshDestroy(out.parts[i].mesh);
			out.parts[i].ready = false;
			out.parts[i].msh = {};
		}
		out.ready = false;
		out.part_count = 0;
		out.look = 102;
		out.bon = {};
		out.ani = {};
		out.center[0] = out.center[1] = out.center[2] = 0.f;
		out.radius = 1.f;
	}

	bool LoadDxvkFieldMesh(WYDD3D9NativeDevice& dxvk, FieldMeshInst& inst, const char* path)
	{
		WYDMsaMesh src;
		if (!WYD_LoadMsaMesh(path, src) || src.vertex_count == 0)
			return false;
		if (!WYD_D3D9NativeMeshUpload(dxvk, inst.mesh, src, 4))
			return false;
		inst.mesh.name = path;
		WYDWytImage tex {};
		if (WYD_LoadTextureHintRgba(src.texture_hint.c_str(), tex))
			(void)WYD_D3D9NativeMeshSetTextureRgba(dxvk, inst.mesh,
				tex.rgba.data(), tex.width, tex.height);
		return true;
	}

	bool LoadDxvkCharSkin(WYDD3D9NativeDevice& dxvk, CharSkinBundle& out,
		const WYDBoneAniCatalog& catalog, const WYDValidAniIndex& validAni,
		int boneAniIndex, int look, int aniClip)
	{
		DestroyCharSkin(out);
		out.look = look;
		const WYDBoneAniEntry* ent = WYD_BoneAniFind(catalog, boneAniIndex);
		if (!ent)
		{
			std::fprintf(stderr, "[project] BoneAni index %d ausente\n", boneAniIndex);
			return false;
		}

		const std::string bonPath = ent->prefix + ".bon";
		const std::string aniPath = WYD_BoneAniClipPath(*ent, validAni, aniClip);
		if (!WYD_LoadBonHierarchy(bonPath.c_str(), out.bon))
			return false;
		if (!WYD_LoadAniClip(aniPath.c_str(), out.ani))
		{
			std::fprintf(stderr, "[project] ani clip falhou: %s\n", aniPath.c_str());
			return false;
		}

		float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
		bool haveBox = false;
		const int maxParts = (ent->num_parts > 8) ? 8 : ent->num_parts;
		for (int part = 1; part <= maxParts; ++part)
		{
			const std::string mshPath = WYD_BoneAniPartPath(*ent, part, look, ".msh");
			CharPart& cp = out.parts[out.part_count];
			if (!WYD_LoadMshMesh(mshPath.c_str(), cp.msh))
				continue;
			if (!WYD_D3D9NativeMeshUploadSkinned(dxvk, cp.mesh, cp.msh))
				continue;

			const std::string wysPath = WYD_BoneAniPartPath(*ent, part, look, ".wys");
			WYDWytImage tex {};
			if (WYD_LoadWysRgba(wysPath.c_str(), tex))
			{
				(void)WYD_D3D9NativeMeshSetTextureRgba(dxvk, cp.mesh,
					tex.rgba.data(), tex.width, tex.height);
			}
			cp.ready = true;
			if (!haveBox)
			{
				minX = cp.msh.min_x; maxX = cp.msh.max_x;
				minY = cp.msh.min_y; maxY = cp.msh.max_y;
				minZ = cp.msh.min_z; maxZ = cp.msh.max_z;
				haveBox = true;
			}
			else
			{
				if (cp.msh.min_x < minX) minX = cp.msh.min_x;
				if (cp.msh.max_x > maxX) maxX = cp.msh.max_x;
				if (cp.msh.min_y < minY) minY = cp.msh.min_y;
				if (cp.msh.max_y > maxY) maxY = cp.msh.max_y;
				if (cp.msh.min_z < minZ) minZ = cp.msh.min_z;
				if (cp.msh.max_z > maxZ) maxZ = cp.msh.max_z;
			}
			++out.part_count;
		}

		if (out.part_count <= 0)
			return false;

		out.center[0] = 0.5f * (minX + maxX);
		out.center[1] = 0.5f * (minY + maxY);
		out.center[2] = 0.5f * (minZ + maxZ);
		const float dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
		out.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
		if (out.radius < 0.01f)
			out.radius = 1.f;
		out.ready = true;
		std::fprintf(stderr,
			"[project] char multi-part prefix=%s look=%d parts=%d radius=%.3f\n",
			ent->prefix.c_str(), look, out.part_count, out.radius);
		return true;
	}
}

int main(int argc, char** argv)
{
	const char* host = (argc > 1) ? argv[1] : EnvOr("WYD_SERVER_HOST", "127.0.0.1");
	const int port = (argc > 2) ? std::atoi(argv[2]) : std::atoi(EnvOr("WYD_SERVER_PORT", "8281"));
	const char* account = EnvOr("WYD_ACCOUNT", "test");
	const char* password = EnvOr("WYD_PASSWORD", "test");
	const char* slotEnv = std::getenv("WYD_CHAR_SLOT");
	const int autoSlot = (slotEnv && slotEnv[0]) ? std::atoi(slotEnv) : -1;
	const bool field2dOnly = WantField2DOnly();
	const bool tryDxvk = WantDxvkNative();

	std::fprintf(stderr, "[project] boot Linux/Wayland — assets=%s\n",
		WYD_AssetRoot().c_str());

	WYDDx9ShaderCatalog shaders;
	if (!WYD_LoadDx9ShaderCatalog(shaders))
		return 1;

	WYDDx9ShaderRuntime dx9rt;
	if (!WYD_Dx9RuntimeBuild(dx9rt, shaders))
	{
		std::fprintf(stderr, "[project] dx9 runtime: create=%d fail=%d\n",
			dx9rt.created, dx9rt.failed);
		return 1;
	}

	WYDObjectMaskTable masks;
	if (!WYD_LoadObjectMask(masks))
		return 2;

	WYDServerNameList servers;
	if (!WYD_LoadServerNameList(servers))
	{
		std::fprintf(stderr, "[project] sn.bin: %s\n", servers.error.c_str());
		return 3;
	}

	WYDWytImage logo;
	if (!WYD_LoadWytRgba("UI/logo1.wyt", logo))
	{
		std::fprintf(stderr, "[project] logo: %s\n", logo.error.c_str());
		return 4;
	}

	WYDMsaMesh mesh;
	if (!WYD_LoadMsaMesh("Effect/sphere.msa", mesh))
		std::fprintf(stderr, "[project] MSA: %s (campo 3D limitado)\n", mesh.error.c_str());

	if (tryDxvk)
		setenv("DXVK_WSI_DRIVER", "SDL2", 0);

	WYDWaylandWindow win;
	if (!WYD_CreateWaylandWindow(win, "WYD 7.48 Linux", 800, 600, false, true))
		return 5;

	WYDAudio audio;
	WYD_AudioInit(audio);

	WYDD3D9NativeDevice dxvk {};
	FieldMeshInst fieldMeshes[5] {};
	int fieldMeshCount = 0;
	CharSkinBundle charSkin {};
	bool charSkinLoaded = false;
	WYDBoneAniCatalog boneAni {};
	WYDValidAniIndex validAni {};
	(void)WYD_LoadBoneAniCatalog(boneAni);
	(void)WYD_LoadValidAniIndex(validAni);
	// look 1 = skinmesh2+ (Fase Q); look 102 = só skinmesh1 (Fase P)
	const int charLook = std::atoi(EnvOr("WYD_CHAR_LOOK", "1"));
	const int charBoneAni = std::atoi(EnvOr("WYD_CHAR_BONEANI", "0"));
	const int charAniClip = std::atoi(EnvOr("WYD_CHAR_ANI", "0"));
	WYDVulkanTextured gpu;
	WYDVulkanMesh vmesh;
	bool useDxvk = false;
	bool gpu2d = false;
	bool gpu3d = false;

	if (tryDxvk && WYD_D3D9NativeLoad(dxvk) &&
		WYD_D3D9NativeCreateDevice(dxvk, WYD_GetSDLWindow(win), win.width, win.height))
	{
		useDxvk = true;
		const int n = WYD_D3D9NativeCreateShadersFromCatalog(dxvk, &shaders);
		std::fprintf(stderr,
			"[project] backend DXVK Native — shaders=%d/%zu (sem swapchain SPIR-V)\n",
			n, shaders.entries.size());
		if (!WYD_D3D9NativeDrawRgba(dxvk, logo.rgba.data(), logo.width, logo.height,
			0xFF101820u))
		{
			std::fprintf(stderr, "[project] splash DXVK falhou\n");
			WYD_D3D9NativeDestroy(dxvk);
			WYD_AudioShutdown(audio);
			WYD_DestroyWaylandWindow(win);
			return 6;
		}
	}
	else
	{
		if (tryDxvk)
			std::fprintf(stderr, "[project] DXVK indisponível (%s) — fallback SPIR-V\n",
				dxvk.status.empty() ? "lib ausente" : dxvk.status.c_str());
		WYD_D3D9NativeDestroy(dxvk);
		dxvk = {};

		if (!WYD_VulkanTexturedCreate(gpu, WYD_GetSDLWindow(win), win.width, win.height) ||
			!WYD_VulkanTexturedUploadRgba(gpu, logo))
		{
			std::fprintf(stderr, "[project] splash vulkan falhou\n");
			WYD_AudioShutdown(audio);
			WYD_DestroyWaylandWindow(win);
			return 6;
		}
		gpu2d = true;
	}

	WYDLoginSession login;
	(void)WYD_LoginConnectAndSend(login, host, port, account, password);
	std::fprintf(stderr, "[project] login: %s\n", login.status.c_str());

	WYDSelCharPanel panel;
	WYDFieldMap fieldMap;
	WYDFieldView fieldView;
	bool showingSelchar = false;
	bool showingField = false;
	bool mode3d = false;
	bool ever3d = false;
	float yaw = 0.f;
	const WYDWytImage* dxvkFrame = &logo;

	if (login.cnf_received)
	{
		showingSelchar = true;
		panel.dirty = true;
		if (autoSlot >= 0)
			TrySelectSlot(login, panel, autoSlot);
	}

	RefreshTitle(WYD_GetSDLWindow(win), servers, login, showingField, mode3d, useDxvk);
	std::fprintf(stderr,
		"[project] ESC sair | 1-4 selchar | WASD campo | T 2D/3D | backend=%s\n",
		useDxvk ? "dxvk-native" : "vulkan-spirv");

	uint32_t frames = 0;
	while (WYD_PollWaylandEvents(win))
	{
		if (login.connected && !login.peer_closed)
			(void)WYD_LoginPoll(login);

		if (login.cnf_received && !showingSelchar && !showingField)
		{
			showingSelchar = true;
			panel.dirty = true;
			if (autoSlot >= 0 && !login.charlogin_sent)
				TrySelectSlot(login, panel, autoSlot);
		}

		if (login.field_entered && !showingField)
		{
			showingField = true;
			showingSelchar = false;
			if (!WYD_FieldMapLoadForWorldPos(fieldMap, login.pos_x, login.pos_y))
				std::fprintf(stderr, "[project] mapa: %s\n", fieldMap.error.c_str());
			WYD_FieldViewResetFromLogin(fieldView, login);

			const char* mock = std::getenv("WYD_LOGIN_MOCK");
			if (mock && mock[0] == '1')
			{
				WYD_FieldWorldInjectMockMobs(login.world,
					login.pos_x, login.pos_y, login.client_id, login.field_mob_name);
				login.create_mob_count = login.world.create_total;
			}

			fieldView.dirty = true;

			const bool try3d = !field2dOnly && mesh.vertex_count > 0;
			if (useDxvk)
			{
				mode3d = false;
				fieldMeshCount = 0;
				struct Spec { const char* path; float ox, oy, oz, scale, spin; };
				const Spec specs[] = {
					{"Effect/sphere.msa",   0.f,  0.f, 0.f, 1.0f, 1.0f},
					{"Effect/sphere2.msa",  2.2f, 0.2f, 0.5f, 0.85f, -0.7f},
					{"Effect/ankh01.msa",  -2.0f, 0.0f, 0.8f, 0.9f, 1.3f},
					{"Effect/arrow.msa",    0.5f, 1.2f,-1.8f, 1.1f, 0.5f},
					{"Effect/FireBall.msa", 1.5f,-0.4f, 2.0f, 0.75f, -1.1f},
				};
				for (const Spec& s : specs)
				{
					if (fieldMeshCount >= 5)
						break;
					FieldMeshInst& inst = fieldMeshes[fieldMeshCount];
					inst.ox = s.ox; inst.oy = s.oy; inst.oz = s.oz;
					inst.scale = s.scale; inst.spinMul = s.spin;
					if (LoadDxvkFieldMesh(dxvk, inst, s.path))
						++fieldMeshCount;
				}
				if (!charSkinLoaded)
				{
					charSkinLoaded = true;
					if (LoadDxvkCharSkin(dxvk, charSkin, boneAni, validAni,
						charBoneAni, charLook, charAniClip))
						std::fprintf(stderr, "[project] char skin multi-part OK\n");
					else
						std::fprintf(stderr, "[project] char skin falhou (Fase Q)\n");
				}
				if (fieldMeshCount > 0 || charSkin.ready)
				{
					mode3d = true;
					ever3d = true;
					std::fprintf(stderr,
						"[project] campo 3D multi-MSA via DXVK count=%d char_parts=%d (T=mapa)\n",
						fieldMeshCount, charSkin.part_count);
				}
				else
				{
					std::fprintf(stderr, "[project] campo 2D DXVK blit — zona %02d%02d mobs=%d\n",
						fieldMap.zone_x, fieldMap.zone_y, login.world.count);
				}
			}
			else if (try3d && EnterMesh3D(gpu, vmesh, WYD_GetSDLWindow(win),
				win.width, win.height, mesh))
			{
				gpu2d = false;
				gpu3d = true;
				mode3d = true;
				ever3d = true;
				std::fprintf(stderr,
					"[project] campo 3D MSA verts=%u idx=%u (Vulkan; T=mapa)\n",
					mesh.vertex_count, static_cast<unsigned>(mesh.indices.size()));
			}
			else
			{
				mode3d = false;
				std::fprintf(stderr, "[project] campo 2D mapa — zona %02d%02d mobs=%d\n",
					fieldMap.zone_x, fieldMap.zone_y, login.world.count);
			}
		}

		if (showingField && win.input.key_toggle_view && mesh.vertex_count > 0)
		{
			if (useDxvk)
			{
				if (mode3d)
				{
					mode3d = false;
					fieldView.dirty = true;
					std::fprintf(stderr, "[project] vista mapa 2D (DXVK)\n");
				}
				else if (fieldMeshCount > 0 || fieldMeshes[0].mesh.ready || charSkin.ready)
				{
					mode3d = true;
					ever3d = true;
					std::fprintf(stderr, "[project] vista multi-mesh 3D (DXVK)\n");
				}
			}
			else if (mode3d)
			{
				if (EnterMap2D(vmesh, gpu, WYD_GetSDLWindow(win), win.width, win.height))
				{
					gpu3d = false;
					gpu2d = true;
					mode3d = false;
					fieldView.dirty = true;
					std::fprintf(stderr, "[project] vista mapa 2D\n");
				}
			}
			else
			{
				if (EnterMesh3D(gpu, vmesh, WYD_GetSDLWindow(win),
					win.width, win.height, mesh))
				{
					gpu2d = false;
					gpu3d = true;
					mode3d = true;
					ever3d = true;
					std::fprintf(stderr, "[project] vista mesh 3D\n");
				}
			}
		}

		if (showingField)
		{
			if (login.world.dirty)
			{
				fieldView.dirty = true;
				login.world.dirty = false;
			}

			const float speed = 0.35f;
			if (win.input.move_x != 0.f || win.input.move_y != 0.f)
			{
				const short ox = static_cast<short>(
					(fieldMap.zone_x << 7) + static_cast<int>(fieldView.local_x * 2.f));
				const short oy = static_cast<short>(
					(fieldMap.zone_y << 7) + static_cast<int>(fieldView.local_y * 2.f));
				WYD_FieldViewMove(fieldView,
					win.input.move_x * speed, win.input.move_y * speed);
				const short nx = static_cast<short>(
					(fieldMap.zone_x << 7) + static_cast<int>(fieldView.local_x * 2.f));
				const short ny = static_cast<short>(
					(fieldMap.zone_y << 7) + static_cast<int>(fieldView.local_y * 2.f));
				(void)WYD_LoginSendAction(login, ox, oy, nx, ny);

				if (win.input.move_x != 0.f)
					yaw += win.input.move_x * 0.08f;

				for (int i = 0; i < WYD_FIELD_MAX_MOBS; ++i)
				{
					if (login.world.mobs[i].alive && login.world.mobs[i].is_self)
					{
						login.world.mobs[i].world_x = nx;
						login.world.mobs[i].world_y = ny;
						break;
					}
				}
			}

			if (!mode3d && fieldView.dirty)
			{
				if (WYD_FieldViewBuild(fieldView, fieldMap, login, login.world,
					win.width, win.height))
				{
					if (useDxvk)
						dxvkFrame = &fieldView.image;
					else if (gpu2d)
						(void)WYD_VulkanTexturedUploadRgba(gpu, fieldView.image);
				}
			}
		}
		else if (showingSelchar)
		{
			if (win.input.mouse_move)
			{
				const int h = WYD_SelCharHitTest(win.width, win.height,
					win.input.mouse_x, win.input.mouse_y);
				if (h != panel.hover)
				{
					panel.hover = h;
					panel.dirty = true;
				}
			}
			if (win.input.key_slot >= 0)
				TrySelectSlot(login, panel, win.input.key_slot);
			if (win.input.mouse_down)
			{
				const int hit = WYD_SelCharHitTest(win.width, win.height,
					win.input.mouse_x, win.input.mouse_y);
				if (hit >= 0)
					TrySelectSlot(login, panel, hit);
			}

			if (panel.dirty)
			{
				if (WYD_SelCharPanelBuild(panel, login, win.width, win.height))
				{
					if (useDxvk)
						dxvkFrame = &panel.image;
					else if (gpu2d)
						(void)WYD_VulkanTexturedUploadRgba(gpu, panel.image);
				}
			}
		}

		if ((frames % 30) == 0)
			RefreshTitle(WYD_GetSDLWindow(win), servers, login, showingField, mode3d, useDxvk);

		bool drawn = false;
		if (useDxvk)
		{
			if (showingField && mode3d && (fieldMeshCount > 0 || charSkin.ready))
			{
				const float spin = yaw + frames * 0.015f;
				const uint32_t argb = StandinClearArgb(dx9rt, frames);
				const float rr = ((argb >> 16) & 0xFF) / 255.f;
				const float gg = ((argb >> 8) & 0xFF) / 255.f;
				const float bb = (argb & 0xFF) / 255.f;
				const int skin = WYD_Dx9RuntimeActiveStandin(dx9rt, frames / 45);
				const int ps = (dxvk.ps_effect_count > 0)
					? static_cast<int>((frames / 90) % static_cast<uint32_t>(dxvk.ps_effect_count))
					: -1;
				float frameR = 1.f;
				for (int i = 0; i < fieldMeshCount; ++i)
				{
					const float rr2 = fieldMeshes[i].mesh.radius * fieldMeshes[i].scale;
					const float d = std::sqrt(
						fieldMeshes[i].ox * fieldMeshes[i].ox +
						fieldMeshes[i].oy * fieldMeshes[i].oy +
						fieldMeshes[i].oz * fieldMeshes[i].oz) + rr2;
					if (d > frameR)
						frameR = d;
				}
				if (charSkin.ready)
				{
					const float cr = charSkin.radius * 2.2f;
					if (cr > frameR)
						frameR = cr;
				}
				drawn = WYD_D3D9NativeSceneBegin(dxvk, 0xFF0A0F1Au);
				if (drawn)
				{
					int partsDrawn = 0;
					if (charSkin.ready)
					{
						const float a = spin * 0.35f;
						const uint32_t tick = (frames / 4u) % (charSkin.ani.ticks ? charSkin.ani.ticks : 1u);
						for (int pi = 0; pi < charSkin.part_count; ++pi)
						{
							CharPart& cp = charSkin.parts[pi];
							if (!cp.ready)
								continue;
							float palette[20 * 16];
							const uint32_t pal = cp.msh.palette;
							if (pal == 0 || pal > 20)
								continue;
							if (!WYD_EvalBonePalette(charSkin.bon, charSkin.ani, tick,
								cp.msh.bind_pose.data(), cp.msh.bone_names.data(),
								pal, palette))
								continue;
							// Sem pseffect no corpo — PS de efeito destrói a textura do char.
							if (!WYD_D3D9NativeMeshDrawInScene(dxvk, cp.mesh, a,
								1.f, 1.f, 1.f, 0, -1, 0.f, 0.f, 0.f, 2.2f,
								0.f, 0.3f, 0.f, frameR * 1.15f, 0.f,
								palette, pal))
							{
								drawn = false;
								break;
							}
							++partsDrawn;
						}
						if (drawn && partsDrawn >= 2)
						{
							dxvk.multi_part_ok = true;
							dxvk.char_parts_drawn = partsDrawn;
						}
					}
					// FX MSA só se não houver personagem (evita cena “lixo visual”).
					const bool drawFx = !charSkin.ready ||
						(std::getenv("WYD_FIELD_FX") && std::getenv("WYD_FIELD_FX")[0] == '1');
					for (int i = 0; drawn && drawFx && i < fieldMeshCount; ++i)
					{
						const FieldMeshInst& inst = fieldMeshes[i];
						if (!inst.mesh.ready)
							continue;
						const float a = spin * inst.spinMul + i * 0.4f;
						const float boneTime = static_cast<float>(frames) * 0.05f;
						if (!WYD_D3D9NativeMeshDrawInScene(dxvk, inst.mesh, a, rr, gg, bb,
							skin, ps, inst.ox, inst.oy, inst.oz, inst.scale,
							0.f, 0.3f, 0.f, frameR * 1.15f, boneTime))
						{
							drawn = false;
							break;
						}
					}
					if (drawn)
						drawn = WYD_D3D9NativeSceneEnd(dxvk);
					else if (dxvk.scene_open)
						(void)WYD_D3D9NativeSceneEnd(dxvk);
				}
			}
			else if (dxvkFrame && !dxvkFrame->rgba.empty())
			{
				drawn = WYD_D3D9NativeDrawRgba(dxvk,
					dxvkFrame->rgba.data(), dxvkFrame->width, dxvkFrame->height,
					0xFF0A0C10u);
			}
			else
			{
				drawn = WYD_D3D9NativeClear(dxvk, 0xFF102040u) &&
					WYD_D3D9NativePresent(dxvk);
			}
		}
		else if (mode3d && gpu3d)
		{
			const float spin = yaw + frames * 0.015f;
			const uint32_t argb = StandinClearArgb(dx9rt, frames);
			const float r = ((argb >> 16) & 0xFF) / 255.f;
			const float g = ((argb >> 8) & 0xFF) / 255.f;
			const float b = (argb & 0xFF) / 255.f;
			drawn = WYD_VulkanMeshDraw(vmesh, spin, r, g, b);
		}
		else if (gpu2d)
		{
			drawn = WYD_VulkanTexturedDraw(gpu);
		}
		if (!drawn)
			break;
		++frames;
		SDL_Delay(16);
	}

	const int mapOk = fieldMap.loaded ? 1 : 0;
	const int zoneX = fieldMap.zone_x;
	const int zoneY = fieldMap.zone_y;
	const int mesh3d = ever3d ? 1 : 0;
	const int shaderDraw = (useDxvk && dxvk.shader_draw_ok) ? 1 : 0;
	const int texDraw = (useDxvk && dxvk.texture_draw_ok) ? 1 : 0;
	const int psDraw = (useDxvk && dxvk.pixel_shader_draw_ok) ? 1 : 0;
	const int gpuDraw = (useDxvk && dxvk.gpu_draw_ok) ? 1 : 0;
	const int multiMesh = (useDxvk && dxvk.multi_mesh_ok) ? 1 : 0;
	const int boneOk = (useDxvk && dxvk.bone_ok) ? 1 : 0;
	const int boneReal = (useDxvk && dxvk.bone_real_ok) ? 1 : 0;
	const int multiPart = (useDxvk && dxvk.multi_part_ok) ? 1 : 0;
	const int skinMulti = (useDxvk && dxvk.skin_multi_ok) ? 1 : 0;
	const int charParts = useDxvk ? dxvk.char_parts_drawn : 0;
	WYD_FieldViewDestroy(fieldView);
	WYD_FieldMapUnload(fieldMap);
	WYD_SelCharPanelDestroy(panel);
	WYD_LoginClose(login);
	if (useDxvk)
	{
		DestroyCharSkin(charSkin);
		for (int i = 0; i < fieldMeshCount; ++i)
			WYD_D3D9NativeMeshDestroy(fieldMeshes[i].mesh);
		WYD_D3D9NativeDestroy(dxvk);
	}
	else
	{
		if (gpu3d)
			WYD_VulkanMeshDestroy(vmesh);
		if (gpu2d)
			WYD_VulkanTexturedDestroy(gpu);
	}
	WYD_AudioShutdown(audio);
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr,
		"[project] fim frames=%u field=%d map=%d mesh3d=%d meshes=%d dx9standin=%d dxvk=%d shaderdraw=%d tex=%d ps=%d gpu=%d multi=%d bone=%d bone_real=%d parts=%d multiparts=%d skin2=%d zone=%02d%02d mobs=%d\n",
		frames,
		login.field_entered ? 1 : 0,
		mapOk,
		mesh3d,
		fieldMeshCount,
		dx9rt.standin_ready,
		useDxvk ? 1 : 0,
		shaderDraw,
		texDraw,
		psDraw,
		gpuDraw,
		multiMesh,
		boneOk,
		boneReal,
		charParts,
		multiPart,
		skinMulti,
		zoneX, zoneY,
		login.world.count);
	return 0;
}
