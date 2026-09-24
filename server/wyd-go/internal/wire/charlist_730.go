package wire

import (
	"wydgo/internal/model"
)

// Layout MSG_CNFAccountLogin do WYD.exe 7.30 (FUN_0049abcf + tabela de sizes):
//
//	Header 12 | SelChar 744 @12 | Cargo 1024 @756 | Coin @1780 |
//	AccountName[16] @1784 | SecretCode[16] @1800 | SSN1/SSN2 @1816 | total 1824 (0x720)
//
// Opcode de sucesso no login: 0x10E.
// Confirmacao create/delete (selchar): 0x110 / 0x112 com Size 756 (0x2F4) =
// Header + SelChar apenas.
// STRUCT_SELCHAR legado: Score ushort 28B x4, Equip[4][16], Exp uint32[4].
const (
	charList730Size             = 1824 // 0x720 — exigido pelo validador do client
	charList730SelOff           = 12
	charList730SelSize          = 744  // 0x2E8
	charList730CargoOff         = 756  // 0x2F4
	charList730CoinOff          = 1780 // 0x6F4
	charList730AccOff           = 1784 // 0x6F8
	charList730SecretOff        = 1800 // 0x708
	selectionUpdate730Size      = 756  // 0x2F4 = Header + SelChar
	score730Size                = 28
	equip730Slots               = 16
)

// CharListFor escolhe a projecao 7.48 ou 7.30. Contas/senha/mundo nao mudam.
func CharListFor(abi ClientABI, accName string, chars []model.Char, cargo []model.Item, cargoGold uint32) []byte {
	if abi == ABI730 {
		return CharList730(accName, chars, cargo, cargoGold)
	}
	return CharList(accName, chars, cargo, cargoGold)
}

// CharList730 monta o CNF de login esperado pelo client 7.30.
func CharList730(accName string, chars []model.Char, cargo []model.Item, cargoGold uint32) []byte {
	b := Build(OpCharList730, SceneCharList, charList730Size)
	putSelChar730(b, charList730SelOff, chars)
	for i := 0; i < len(cargo) && i < model.MaxCargo; i++ {
		PutItem(b, charList730CargoOff+i*8, cargo[i])
	}
	putU32(b, charList730CoinOff, cargoGold)
	copy(b[charList730AccOff:charList730AccOff+16], accName)
	// SecretCode / SSN permanecem zero: a sessao Go autentica por keyword/checksum.
	return b
}

// CharacterSelectionUpdate730 e o 0x110/0x112 do selchar 7.30: so Header+SelChar.
func CharacterSelectionUpdate730(opcode, id uint16, chars []model.Char) []byte {
	b := Build(opcode, id, selectionUpdate730Size)
	putSelChar730(b, charList730SelOff, chars)
	return b
}

func putSelChar730(dst []byte, offset int, chars []model.Char) {
	for slot := 0; slot < 4 && slot < len(chars); slot++ {
		ch := chars[slot]
		if ch.Name == "" {
			continue
		}
		putU16(dst, offset+slot*2, ch.X)
		putU16(dst, offset+8+slot*2, ch.Y)
		copy(dst[offset+16+slot*16:offset+32+slot*16], ch.Name)
		score := EncodeClientScore730(wireScore(ch))
		copy(dst[offset+80+slot*score730Size:offset+80+(slot+1)*score730Size], score[:])
		for equipSlot, item := range ch.Equip {
			if equipSlot >= equip730Slots {
				break
			}
			PutItem(dst, offset+192+(slot*equip730Slots+equipSlot)*8, item)
		}
		putU16(dst, offset+704+slot*2, GuildWireID(ch.GuildID))
		putU32(dst, offset+712+slot*4, ch.Gold)
		putU32(dst, offset+728+slot*4, clampU32(uint64(ch.Exp)))
	}
}

// EncodeClientScore730 serializa STRUCT_SCORE legado (28 bytes, campos ushort).
// Layout fiel ao STRUCT_SCORE_OLD / clients pre-uint32:
//
//	Level, Ac, Damage, Reserved, AttackRun, MaxHp, MaxMp, Hp, Mp,
//	Str, Int, Dex, Con, Special[4]
func EncodeClientScore730(score *model.Score) [score730Size]byte {
	var out [score730Size]byte
	if score == nil {
		return out
	}
	putU16(out[:], 0, clampU16(score.Level))
	putU16(out[:], 2, clampU16(score.Defense))
	putU16(out[:], 4, clampU16(score.Attack))
	out[6] = clampByte(int(score.Merchant))
	out[7] = clampByte(int(score.AttackRun))
	putU16(out[:], 8, clampU16(score.MaxHP))
	putU16(out[:], 10, clampU16(score.MaxMP))
	putU16(out[:], 12, clampU16(score.CurHP))
	putU16(out[:], 14, clampU16(score.CurMP))
	putU16(out[:], 16, clampU16(score.Str))
	putU16(out[:], 18, clampU16(score.Int))
	putU16(out[:], 20, clampU16(score.Dex))
	putU16(out[:], 22, clampU16(score.Con))
	out[24] = clampByte(int(score.Mastery[0]))
	out[25] = clampByte(int(score.Mastery[1]))
	out[26] = clampByte(int(score.Mastery[2]))
	out[27] = clampByte(int(score.Mastery[3]))
	return out
}

func clampU16(v uint32) uint16 {
	if v > 0xffff {
		return 0xffff
	}
	return uint16(v)
}

func clampU32(v uint64) uint32 {
	if v > 0xffffffff {
		return 0xffffffff
	}
	return uint32(v)
}
