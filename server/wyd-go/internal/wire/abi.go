package wire

// ClientABI seleciona a projecao on-wire sem alterar o modelo autoritativo.
// O client manda a versao em MSG_AccountLogin.ClientVersion (@40).
type ClientABI uint16

const (
	// ABI748 e o layout canonico deste servidor (TMProject / WYD-Go).
	ABI748 ClientABI = 748
	// ABI730 e o client WYD.exe 7.30 (cliver tipico 730 / 7300).
	ABI730 ClientABI = 730
)

// ABIFromCliver mapeia o ClientVersion do login para a ABI de resposta.
// Qualquer build < 748 usa o pacote legado 7.30; 748+ permanece intacto.
func ABIFromCliver(cliver uint32) ClientABI {
	if cliver > 0 && cliver < 748 {
		return ABI730
	}
	// Alguns clients 7.30 reportam 7300 (VERSION*10).
	if cliver >= 7300 && cliver < 7480 {
		return ABI730
	}
	return ABI748
}
