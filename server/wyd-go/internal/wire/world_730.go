package wire

import (
	"time"

	"wydgo/internal/model"
)

// Tamanhos on-wire do client 7.30 (tabela de validacao do WYD.exe).
const (
	enterWorld730Size  = 956 // 0x3BC
	createMob730Size   = 164 // 0xA4
	updateScore730Size = 84  // 0x54
	hpMp730Size        = 20  // 0x14
	mob730Size         = 756 // 0x2F4 — STRUCT_MOB dentro do EnterWorld
)

// EnterWorldFor escolhe a projecao 7.48 ou 7.30.
func EnterWorldFor(abi ClientABI, id, slot uint16, ch model.Char) []byte {
	if abi == ABI730 {
		return EnterWorld730(id, slot, ch)
	}
	return EnterWorld(id, slot, ch)
}

// EnterWorld730: Header + Pos + STRUCT_MOB(756) + Slot/ID + ShortSkill[16] + Hold.
// Offsets confirmados em FUN_00472625.
func EnterWorld730(id, slot uint16, ch model.Char) []byte {
	b := Build(OpEnterWorld, id, enterWorld730Size)
	putU16(b, 12, ch.X)
	putU16(b, 14, ch.Y)
	putMob730(b, 16, ch)
	putU16(b, 0x306, slot)
	putU16(b, 0x308, id)
	copy(b[0x30A:0x30A+16], ch.ShortSkill[:16])
	putU32(b, 0x31C, ch.Hold)
	return b
}

// putMob730 grava STRUCT_MOB legado (756B): Score ushort 28B, Equip[16], Carry[64].
// Layout classico (Coin int, Exp int32, LearnedSkill unico, ScoreBonus/Special/Skill).
func putMob730(dst []byte, off int, ch model.Char) {
	copy(dst[off:off+16], ch.Name)
	dst[off+12] = CPNameByte(ch.CP)
	score := wireScore(ch)
	if score != nil {
		dst[off+17] = clampByte(int(score.Merchant))
	}
	putU16(dst, off+18, GuildWireID(ch.GuildID))
	dst[off+20] = ch.Class
	putU32(dst, off+24, ch.Gold)
	putU32(dst, off+28, clampU32(uint64(ch.Exp)))
	putU16(dst, off+32, ch.X)
	putU16(dst, off+34, ch.Y)
	base := EncodeClientScore730(ch.Score)
	runtime := EncodeClientScore730(score)
	copy(dst[off+36:off+64], base[:])
	copy(dst[off+64:off+92], runtime[:])
	for i, item := range ch.Equip {
		if i >= equip730Slots {
			break
		}
		PutItem(dst, off+92+i*8, item)
	}
	for i, item := range ch.Inv {
		if i >= 64 {
			break
		}
		PutItem(dst, off+220+i*8, item)
	}
	putU32(dst, off+732, ch.LearnedSkill)
	if score != nil {
		putU16(dst, off+736, clampU16(score.StatusPts))
		putU16(dst, off+738, clampU16(score.MasterPts))
		putU16(dst, off+740, clampU16(score.SkillPts))
		dst[off+742] = clampByte(int(score.Critical))
		dst[off+743] = clampByte(int(score.SaveMana))
		dst[off+749] = clampByte(int(score.MagicAmp))
		dst[off+750] = clampByte(int(score.RegenHP))
		dst[off+751] = clampByte(int(score.RegenMP))
		dst[off+752] = clampByte(int(score.ResistFire))
		dst[off+753] = clampByte(int(score.ResistIce))
		dst[off+754] = clampByte(int(score.ResistHoly))
		dst[off+755] = clampByte(int(score.ResistThunder))
	}
	copy(dst[off+744:off+748], ch.ShortSkill[:4])
	dst[off+748] = ch.GuildRank
}

// Offsets MSG_CreateMob 7.30 (164B = 0xA4). Confirmados em FUN_0046fced:
// memcpy Score 28B a partir de 0x5C; Anct via FUN_00509627 em 0x7A; Tab em 0x8A.
// NAO reutilizar o layout 7.48 (328B / Score@140) — desalinha CurHP e o client
// interpreta o NPC como morto (pose caída).
const (
	createMob730PosOff    = 12
	createMob730IDOff     = 16
	createMob730NameOff   = 18
	createMob730EquipOff  = 34  // 0x22 — 16 WORDs de mesh
	createMob730AffectOff = 66  // 0x42 — Affect[12] ushort (24B)
	createMob730GuildOff  = 90  // 0x5A
	createMob730ScoreOff  = 92  // 0x5C — STRUCT_SCORE 28B (CurHP @ +12)
	createMob730SpawnOff  = 120 // 0x78 — CreateType
	createMob730AnctOff   = 122 // 0x7A — AnctCode[16]
	createMob730TabOff    = 138 // 0x8A — Tab[26]
	createMob730AffectN   = 12
	createMob730ScoreHP   = createMob730ScoreOff + 12 // Hp ushort dentro do Score
)

// CreateMobFor / CreateMobWithGuildRankFor projetam o spawn 7.30 (164B).
func CreateMobWithGuildRankFor(abi ClientABI, id uint16, name string, x, y uint16, mesh []uint16, anct []byte,
	score *model.Score, affects []model.Affect, spawn, guild uint16, guildRank byte, cp int16) []byte {
	if abi == ABI730 {
		return CreateMob730(id, name, x, y, mesh, anct, score, affects, spawn, guild, guildRank, &cp)
	}
	return CreateMobWithGuildRank(id, name, x, y, mesh, anct, score, affects, spawn, guild, guildRank, cp)
}

func CreateMobVisualFor(abi ClientABI, id uint16, name string, x, y uint16, mesh []uint16, anct []byte,
	score *model.Score, affects []model.Affect, spawn uint16) []byte {
	if abi == ABI730 {
		return CreateMob730(id, name, x, y, mesh, anct, score, affects, spawn, 0, 0, nil)
	}
	return CreateMobVisual(id, name, x, y, mesh, anct, score, affects, spawn)
}

// CreateMob730 monta o 0x364 legado do WYD.exe 7.30.
// guildRank nao cabe entre Guild e Score neste ABI (Score vem logo apos Guild);
// o client le Merchant do Score para tipo de NPC/loja.
func CreateMob730(id uint16, name string, x, y uint16, mesh []uint16, anct []byte, ext *model.Score,
	affects []model.Affect, spawn, guild uint16, guildRank byte, cp *int16) []byte {
	_ = guildRank // reservado: ABI730 nao tem GuildMemberType antes do Score
	b := Build(OpCreateMob, SceneField, createMob730Size)
	putU16(b, createMob730PosOff, x)
	putU16(b, createMob730PosOff+2, y)
	putU16(b, createMob730IDOff, id)
	copy(b[createMob730NameOff:createMob730NameOff+16], name)
	if cp != nil {
		b[createMob730NameOff+12] = CPNameByte(*cp)
	}
	for i := 0; i < equip730Slots; i++ {
		var v uint16
		if i < len(mesh) {
			v = mesh[i]
		}
		putU16(b, createMob730EquipOff+i*2, v)
	}
	putCreateMobAffect730(b, createMob730AffectOff, affects)
	putU16(b, createMob730GuildOff, GuildWireID(guild))
	score := EncodeClientScore730(ext)
	copy(b[createMob730ScoreOff:createMob730ScoreOff+score730Size], score[:])
	putU16(b, createMob730SpawnOff, spawn)
	if len(anct) > 0 {
		n := len(anct)
		if n > 16 {
			n = 16
		}
		copy(b[createMob730AnctOff:createMob730AnctOff+n], anct[:n])
	}
	return b
}

func putCreateMobAffect730(dst []byte, offset int, affects []model.Affect) {
	for i := 0; i < createMob730AffectN; i++ {
		if i >= len(affects) || affects[i].Type == 0 {
			continue
		}
		putU16(dst, offset+i*2, uint16(affects[i].Type))
	}
}

// UpdateScoreFor / MobScoreFor — 7.30 usa Score 28B + affects curtos (84B total).
func UpdateScoreFor(abi ClientABI, id uint16, ch model.Char) []byte {
	if abi == ABI730 {
		return UpdateScore730(id, ch)
	}
	return UpdateScore(id, ch)
}

func MobScoreFor(abi ClientABI, id uint16, scoreState *model.Score, affects []model.Affect) []byte {
	if abi == ABI730 {
		return MobScore730(id, scoreState, affects)
	}
	return MobScore(id, scoreState, affects)
}

func UpdateScore730(id uint16, ch model.Char) []byte {
	b := Build(OpUpdateScore, id, updateScore730Size)
	score := EncodeClientScore730(wireScore(ch))
	copy(b[12:40], score[:])
	putAffectBlock730(b, 40, ch.Affects[:])
	putU16(b, 72, GuildWireID(ch.GuildID))
	putU16(b, 74, uint16(ch.GuildRank))
	putU16(b, 76, 0) // ReqHp
	putU16(b, 78, 0) // ReqMp
	putU16(b, 80, 0)
	putU16(b, 82, 0)
	return b
}

func MobScore730(id uint16, scoreState *model.Score, affects []model.Affect) []byte {
	b := Build(OpUpdateScore, id, updateScore730Size)
	score := EncodeClientScore730(scoreState)
	copy(b[12:40], score[:])
	putAffectBlock730(b, 40, affects)
	return b
}

func putAffectBlock730(dst []byte, offset int, affects []model.Affect) {
	now := time.Now()
	for i := 0; i < 4 && i < len(affects); i++ {
		a := affects[i]
		units := affectTimeUnits(a.ExpiresAt, now)
		if a.Type == 0 || units == 0 {
			continue
		}
		dst[offset+i*8] = byte(a.Type)
		dst[offset+i*8+1] = clampByte(int(a.Level))
		putU16(dst, offset+i*8+2, uint16(int16(clampAffectValue(a.Value))))
		putU32(dst, offset+i*8+4, units)
	}
}

func clampAffectValue(v int) int {
	if v < -32768 {
		return -32768
	}
	if v > 32767 {
		return 32767
	}
	return v
}

// HpMpFor — 7.30 usa quatro WORDs (20B); 7.48 usa quatro DWORDs (28B).
func HpMpFor(abi ClientABI, id uint16, score *model.Score) []byte {
	if abi == ABI730 {
		return HpMp730(id, score)
	}
	return HpMp(id, score)
}

func MobHpMpFor(abi ClientABI, id uint16, currentHP, maxHP, currentMP, maxMP uint32) []byte {
	return HpMpFor(abi, id, &model.Score{
		Version: model.ScoreVersion,
		CurHP:   currentHP, MaxHP: maxHP,
		CurMP: currentMP, MaxMP: maxMP,
	})
}

func HpMp730(id uint16, score *model.Score) []byte {
	if score == nil {
		score = &model.Score{Version: model.ScoreVersion}
	}
	b := Build(OpSetHpMp, id, hpMp730Size)
	putU16(b, 12, clampU16(score.CurHP))
	putU16(b, 14, clampU16(score.CurMP))
	putU16(b, 16, clampU16(score.MaxHP))
	putU16(b, 18, clampU16(score.MaxMP))
	return b
}

// SupportsUpdateAffects: 7.30 nao lista 0x3B9 na tabela de sizes.
func SupportsUpdateAffects(abi ClientABI) bool {
	return abi != ABI730
}

// UpdateAffectsFor devolve o 0x3B9 7.48, ou nil quando o ABI nao aceita o opcode.
func UpdateAffectsFor(abi ClientABI, id uint16, ch model.Char) []byte {
	if !SupportsUpdateAffects(abi) {
		return nil
	}
	return UpdateAffects(id, ch)
}
