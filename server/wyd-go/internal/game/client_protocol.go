package game

import (
	gameNet "wydgo/internal/net"
	"wydgo/internal/wire"
)

// playerScorePacket is the only private score publication path for a live
// player. Keeping the ABI choice next to the recipient prevents a source
// client from receiving a stock 28-byte STRUCT_SCORE after a later feature.
func playerScorePacket(p *Player) []byte {
	if p == nil || p.Session == nil || p.Char == nil {
		return nil
	}
	return wire.UpdateScoreFor(p.ClientABI, p.ID, *p.Char)
}

// playerAffectsPacket follows the same recipient-owned ABI rule as score.
// ABI730 omite 0x3B9 (nil) — o caller deve checar len antes de Session.Send.
func playerAffectsPacket(p *Player) []byte {
	if p == nil || p.Session == nil || p.Char == nil {
		return nil
	}
	return wire.UpdateAffectsFor(p.ClientABI, p.ID, *p.Char)
}

// observedPlayerScorePacket serializes one subject for a specific observer ABI.
func observedPlayerScorePacket(observer, subject *Player) []byte {
	if observer == nil || observer.Session == nil || subject == nil || subject.Char == nil {
		return nil
	}
	return wire.UpdateScoreFor(observer.ClientABI, subject.ID, *subject.Char)
}

// selectionUpdatePacket rebuilds the four-character selection aggregate after
// create/delete/evolution using the ABI negotiated by the login packet.
// Clients 7.30 expect 0x110/0x112 with Size 756 (Header+SelChar legado), not
// the 7.48 1288B delta nor a reenvio completo do CNF de login.
func selectionUpdatePacket(s *gameNet.Session, opcode, id uint16, p *Player) []byte {
	if s == nil || p == nil || p.Account == nil {
		return nil
	}
	if p.ClientABI == wire.ABI730 {
		return wire.CharacterSelectionUpdate730(opcode, id, p.Account.Chars)
	}
	return wire.CharacterSelectionUpdate(opcode, id, p.Account.Chars)
}

// characterListPacket is used when a feature returns to character selection;
// login and re-entry must advertise an identical aggregate layout.
func characterListPacket(s *gameNet.Session, p *Player) []byte {
	if s == nil || p == nil || p.Account == nil {
		return nil
	}
	return wire.CharListFor(p.ClientABI, p.Account.Name, p.Account.Chars, p.Account.Cargo[:], p.Account.CargoGold)
}
