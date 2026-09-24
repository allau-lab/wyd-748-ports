package wire

import (
	"encoding/binary"
	"testing"

	"wydgo/internal/model"
)

func TestABIFromCliver(t *testing.T) {
	cases := []struct {
		cliver uint32
		want   ClientABI
	}{
		{0, ABI748},
		{730, ABI730},
		{7300, ABI730},
		{747, ABI730},
		{748, ABI748},
		{7480, ABI748},
		{10000, ABI748},
	}
	for _, tc := range cases {
		if got := ABIFromCliver(tc.cliver); got != tc.want {
			t.Fatalf("cliver=%d abi=%d want=%d", tc.cliver, got, tc.want)
		}
	}
}

func TestCharList730Layout(t *testing.T) {
	ch := model.Char{
		Name: "Tester",
		X:    2100, Y: 2100,
		Gold: 1234, Exp: 999,
		GuildID: 7,
		Score: &model.Score{
			Version: model.ScoreVersion,
			Level:   50, Attack: 100, Defense: 80,
			MaxHP: 500, MaxMP: 200, CurHP: 400, CurMP: 150,
			Str: 20, Int: 10, Dex: 15, Con: 12,
			AttackRun: 3, Merchant: 0,
			Mastery: [4]uint32{1, 2, 3, 4},
		},
		Equip: [model.MaxEquipSlots]model.Item{{Index: 1100}},
	}
	cargo := make([]model.Item, 2)
	cargo[0] = model.Item{Index: 400}
	b := CharList730("gilmar", []model.Char{ch}, cargo, 55)
	if len(b) != 1824 {
		t.Fatalf("len=%d want 1824", len(b))
	}
	h := ParseHeader(b)
	if h.Type != OpCharList730 {
		t.Fatalf("opcode=0x%X want 0x10E", h.Type)
	}
	if h.ID != SceneCharList {
		t.Fatalf("id=0x%X", h.ID)
	}
	// Name in SelChar @12+16
	name := string(b[12+16 : 12+32])
	if name[:6] != "Tester" {
		t.Fatalf("name=%q", name)
	}
	// Score Level ushort @12+80
	if binary.LittleEndian.Uint16(b[12+80:12+82]) != 50 {
		t.Fatalf("level=%d", binary.LittleEndian.Uint16(b[12+80:12+82]))
	}
	// Equip[0] @12+192
	if binary.LittleEndian.Uint16(b[12+192:12+194]) != 1100 {
		t.Fatalf("equip=%d", binary.LittleEndian.Uint16(b[12+192:12+194]))
	}
	// Cargo[0] @756
	if binary.LittleEndian.Uint16(b[756:758]) != 400 {
		t.Fatalf("cargo=%d", binary.LittleEndian.Uint16(b[756:758]))
	}
	if binary.LittleEndian.Uint32(b[1780:1784]) != 55 {
		t.Fatalf("coin=%d", binary.LittleEndian.Uint32(b[1780:1784]))
	}
	if string(b[1784:1790]) != "gilmar" {
		t.Fatalf("acc=%q", b[1784:1800])
	}
	// 748 path unchanged
	b748 := CharListFor(ABI748, "gilmar", []model.Char{ch}, cargo, 55)
	if len(b748) != 2360 || ParseHeader(b748).Type != OpCharList {
		t.Fatalf("748 ABI broke: len=%d type=0x%X", len(b748), ParseHeader(b748).Type)
	}
	bPick := CharListFor(ABI730, "gilmar", nil, nil, 0)
	if len(bPick) != 1824 || ParseHeader(bPick).Type != OpCharList730 {
		t.Fatalf("730 picker failed")
	}
	sel := CharacterSelectionUpdate730(OpCNFNewCharacter, 1, []model.Char{ch})
	if len(sel) != 756 || ParseHeader(sel).Type != OpCNFNewCharacter {
		t.Fatalf("730 selection update: len=%d type=0x%X", len(sel), ParseHeader(sel).Type)
	}
	if string(sel[12+16:12+22]) != "Tester" {
		t.Fatalf("730 selection name=%q", sel[12+16:12+32])
	}
}

func TestEncodeClientScore730Size(t *testing.T) {
	s := EncodeClientScore730(&model.Score{Level: 1, MaxHP: 100, CurHP: 100})
	if len(s) != 28 {
		t.Fatalf("size=%d", len(s))
	}
}
