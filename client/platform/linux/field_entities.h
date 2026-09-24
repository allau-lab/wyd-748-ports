#pragma once

// Entidades de campo a partir de MSG_CreateMob 0x364 (328 B) — Fase F+.
#include "login_account.h"

#include <cstdint>

constexpr int WYD_FIELD_MAX_MOBS = 128;

struct WYDFieldMob
{
	bool alive = false;
	unsigned short id = 0;
	short world_x = 0;
	short world_y = 0;
	unsigned short level = 0;
	char name[16] {};
	bool is_self = false;
};

struct WYDFieldWorld
{
	WYDFieldMob mobs[WYD_FIELD_MAX_MOBS] {};
	int count = 0;       // vivos
	int create_total = 0;
	int remove_total = 0;
	bool dirty = false;
};

// Offsets on-wire MSG_CreateMob (ABI 328).
constexpr std::size_t kCreateMobSize = 328;
constexpr std::size_t kCreateMobOffPosX = 12;
constexpr std::size_t kCreateMobOffPosY = 14;
constexpr std::size_t kCreateMobOffMobID = 16;
constexpr std::size_t kCreateMobOffName = 18;
// Score.Level está em Score+4; Score começa após Guild(2)+GuildLevel(1)+pad.
// Validado via offsetof em compile-time no .cpp com struct espelho.

void WYD_FieldWorldClear(WYDFieldWorld& world);

// Insere/atualiza mob a partir do payload 0x364. self_id marca is_self.
bool WYD_FieldWorldApplyCreateMob(WYDFieldWorld& world, const char* raw,
	unsigned short size, unsigned short self_id);

// Remove por MobID (MSG_RemoveMob).
bool WYD_FieldWorldApplyRemoveMob(WYDFieldWorld& world, const char* raw,
	unsigned short size);

// Spawns sintéticos ao redor do player (WYD_LOGIN_MOCK).
void WYD_FieldWorldInjectMockMobs(WYDFieldWorld& world,
	short center_x, short center_y, unsigned short self_id, const char* self_name);

int WYD_FieldWorldAliveCount(const WYDFieldWorld& world);
