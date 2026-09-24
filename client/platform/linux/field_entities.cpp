#include "field_entities.h"

#include <cstdio>
#include <cstring>
#include <cstddef>

namespace
{
	constexpr std::size_t kOffLevel = 144; // Score.Level within MSG_CreateMob

	WYDFieldMob* FindById(WYDFieldWorld& world, unsigned short id)
	{
		for (int i = 0; i < WYD_FIELD_MAX_MOBS; ++i)
		{
			if (world.mobs[i].alive && world.mobs[i].id == id)
				return &world.mobs[i];
		}
		return nullptr;
	}

	WYDFieldMob* AllocSlot(WYDFieldWorld& world)
	{
		for (int i = 0; i < WYD_FIELD_MAX_MOBS; ++i)
		{
			if (!world.mobs[i].alive)
				return &world.mobs[i];
		}
		return nullptr;
	}

	void Recount(WYDFieldWorld& world)
	{
		int n = 0;
		for (int i = 0; i < WYD_FIELD_MAX_MOBS; ++i)
			if (world.mobs[i].alive)
				++n;
		world.count = n;
	}
}

void WYD_FieldWorldClear(WYDFieldWorld& world)
{
	world = {};
}

bool WYD_FieldWorldApplyCreateMob(WYDFieldWorld& world, const char* raw,
	unsigned short size, unsigned short self_id)
{
	if (!raw || size < kCreateMobSize)
		return false;

	short posX = 0, posY = 0;
	unsigned short mobId = 0;
	unsigned int level = 0;
	char name[16] {};
	std::memcpy(&posX, raw + kCreateMobOffPosX, sizeof(posX));
	std::memcpy(&posY, raw + kCreateMobOffPosY, sizeof(posY));
	std::memcpy(&mobId, raw + kCreateMobOffMobID, sizeof(mobId));
	std::memcpy(name, raw + kCreateMobOffName, 15);
	std::memcpy(&level, raw + kOffLevel, sizeof(level));

	WYDFieldMob* slot = FindById(world, mobId);
	if (!slot)
		slot = AllocSlot(world);
	if (!slot)
		return false;

	const bool wasAlive = slot->alive;
	slot->alive = true;
	slot->id = mobId;
	slot->world_x = posX;
	slot->world_y = posY;
	slot->level = static_cast<unsigned short>(level > 65535u ? 65535u : level);
	std::memset(slot->name, 0, sizeof(slot->name));
	std::memcpy(slot->name, name, 15);
	slot->is_self = (self_id != 0 && mobId == self_id);

	if (!wasAlive)
		++world.create_total;
	Recount(world);
	world.dirty = true;
	return true;
}

bool WYD_FieldWorldApplyRemoveMob(WYDFieldWorld& world, const char* raw,
	unsigned short size)
{
	if (!raw || size < sizeof(MSG_STANDARD))
		return false;

	const auto* hdr = reinterpret_cast<const MSG_STANDARD*>(raw);
	const unsigned short mobId = hdr->ID;
	WYDFieldMob* slot = FindById(world, mobId);
	if (!slot)
		return false;

	slot->alive = false;
	++world.remove_total;
	Recount(world);
	world.dirty = true;
	return true;
}

void WYD_FieldWorldInjectMockMobs(WYDFieldWorld& world,
	short center_x, short center_y, unsigned short self_id, const char* self_name)
{
	WYD_FieldWorldClear(world);

	alignas(8) char selfPkt[kCreateMobSize] {};
	auto* hdr = reinterpret_cast<MSG_STANDARD*>(selfPkt);
	hdr->Size = static_cast<unsigned short>(kCreateMobSize);
	hdr->Type = MSG_CreateMob_Opcode;
	hdr->ID = self_id;
	short px = center_x, py = center_y;
	unsigned short mid = self_id;
	unsigned int level = 100;
	std::memcpy(selfPkt + kCreateMobOffPosX, &px, 2);
	std::memcpy(selfPkt + kCreateMobOffPosY, &py, 2);
	std::memcpy(selfPkt + kCreateMobOffMobID, &mid, 2);
	if (self_name)
		std::memcpy(selfPkt + kCreateMobOffName, self_name, std::strlen(self_name));
	std::memcpy(selfPkt + kOffLevel, &level, 4);
	WYD_FieldWorldApplyCreateMob(world, selfPkt, hdr->Size, self_id);

	struct Mock { const char* name; int dx; int dy; unsigned short id; unsigned int lv; };
	const Mock mocks[] = {
		{"Guard", 4, 0, 2001, 50},
		{"Merchant", -3, 2, 2002, 1},
		{"Wolf", 6, -4, 2003, 25},
		{"Traveler", -5, -3, 2004, 40},
	};
	for (const Mock& m : mocks)
	{
		alignas(8) char pkt[kCreateMobSize] {};
		auto* h = reinterpret_cast<MSG_STANDARD*>(pkt);
		h->Size = static_cast<unsigned short>(kCreateMobSize);
		h->Type = MSG_CreateMob_Opcode;
		h->ID = m.id;
		short x = static_cast<short>(center_x + m.dx);
		short y = static_cast<short>(center_y + m.dy);
		unsigned short id = m.id;
		std::memcpy(pkt + kCreateMobOffPosX, &x, 2);
		std::memcpy(pkt + kCreateMobOffPosY, &y, 2);
		std::memcpy(pkt + kCreateMobOffMobID, &id, 2);
		std::memcpy(pkt + kCreateMobOffName, m.name, std::strlen(m.name));
		std::memcpy(pkt + kOffLevel, &m.lv, 4);
		WYD_FieldWorldApplyCreateMob(world, pkt, h->Size, self_id);
	}

	std::fprintf(stderr,
		"[WYDLINUX][field] MOCK mobs=%d around %d,%d (self id=%u)\n",
		world.count, center_x, center_y, self_id);
}

int WYD_FieldWorldAliveCount(const WYDFieldWorld& world)
{
	return world.count;
}
