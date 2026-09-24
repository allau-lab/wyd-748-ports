package wire

import (
	"encoding/binary"
	"testing"

	"wydgo/internal/model"
)

func TestEnterWorld730SizeAndOffsets(t *testing.T) {
	ch := model.Char{
		Name:  "Gilmarr",
		Class: 3,
		X:     2100,
		Y:     2100,
		Gold:  100,
		Exp:   50,
		Score: &model.Score{Version: model.ScoreVersion, Level: 1, CurHP: 80, MaxHP: 100, CurMP: 40, MaxMP: 50, Str: 5},
	}
	b := EnterWorldFor(ABI730, 7, 0, ch)
	if len(b) != enterWorld730Size {
		t.Fatalf("size=%d want %d", len(b), enterWorld730Size)
	}
	h := ParseHeader(b)
	if h.Type != OpEnterWorld || h.ID != 7 {
		t.Fatalf("header type=%x id=%d", h.Type, h.ID)
	}
	if binary.LittleEndian.Uint16(b[12:14]) != 2100 || binary.LittleEndian.Uint16(b[14:16]) != 2100 {
		t.Fatalf("pos xy")
	}
	if binary.LittleEndian.Uint16(b[0x306:0x308]) != 0 || binary.LittleEndian.Uint16(b[0x308:0x30A]) != 7 {
		t.Fatalf("slot/id @0x306")
	}
	// BaseScore.Level @ mob+36
	if binary.LittleEndian.Uint16(b[16+36:16+38]) != 1 {
		t.Fatalf("base level=%d", binary.LittleEndian.Uint16(b[16+36:16+38]))
	}
}

func TestCreateMobAndScore730Sizes(t *testing.T) {
	score := &model.Score{Version: model.ScoreVersion, Level: 2, CurHP: 1234, MaxHP: 2000, CurMP: 10, MaxMP: 20}
	cm := CreateMobVisualFor(ABI730, 1001, "Slime", 100, 101, []uint16{22}, []byte{3, 4}, score, nil, 0)
	if len(cm) != createMob730Size {
		t.Fatalf("createmob size=%d", len(cm))
	}
	if binary.LittleEndian.Uint16(cm[createMob730IDOff:createMob730IDOff+2]) != 1001 {
		t.Fatalf("id")
	}
	// Score@0x5C: CurHP desalinhado = NPC caido no client 7.30
	if got := binary.LittleEndian.Uint16(cm[createMob730ScoreHP : createMob730ScoreHP+2]); got != 1234 {
		t.Fatalf("CurHP@%d=%d want 1234 (layout Score errado)", createMob730ScoreHP, got)
	}
	if got := binary.LittleEndian.Uint16(cm[createMob730ScoreOff+8 : createMob730ScoreOff+10]); got != 2000 {
		t.Fatalf("MaxHP=%d", got)
	}
	if got := binary.LittleEndian.Uint16(cm[createMob730SpawnOff : createMob730SpawnOff+2]); got != 0 {
		t.Fatalf("spawn=%d", got)
	}
	if cm[createMob730AnctOff] != 3 || cm[createMob730AnctOff+1] != 4 {
		t.Fatalf("anct")
	}
	// Score NAO pode estar no slot antigo 0x7A (agora Anct)
	if binary.LittleEndian.Uint16(cm[122:124]) == 2 && binary.LittleEndian.Uint16(cm[122+12:122+14]) == 1234 {
		t.Fatal("Score ainda gravado em 0x7A (layout antigo)")
	}

	us := UpdateScoreFor(ABI730, 7, model.Char{Name: "A", Score: score})
	if len(us) != updateScore730Size {
		t.Fatalf("updatescore size=%d", len(us))
	}
	hp := HpMpFor(ABI730, 7, score)
	if len(hp) != hpMp730Size {
		t.Fatalf("hpmp size=%d", len(hp))
	}
	if binary.LittleEndian.Uint16(hp[12:14]) != 1234 {
		t.Fatalf("curhp")
	}
}

func TestABI748WorldPacketsUnchanged(t *testing.T) {
	ch := model.Char{Name: "A", Score: &model.Score{Version: model.ScoreVersion, Level: 1}}
	if len(EnterWorldFor(ABI748, 1, 0, ch)) != 2104 {
		t.Fatal("enter748")
	}
	if len(CreateMobVisualFor(ABI748, 1, "x", 1, 1, nil, nil, ch.Score, nil, 0)) != 328 {
		t.Fatal("createmob748")
	}
	if len(UpdateScoreFor(ABI748, 1, ch)) != 232 {
		t.Fatal("score748")
	}
	if len(HpMpFor(ABI748, 1, ch.Score)) != 28 {
		t.Fatal("hpmp748")
	}
}
