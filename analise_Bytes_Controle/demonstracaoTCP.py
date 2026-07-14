"""Mostra, em tempo real, os payloads HID enviados pela ESP32."""

import argparse
import socket
import time

try:
    from .protocolo_telemetria import (
        ConexaoEncerrada,
        FrameInvalido,
        LeitorFramesTCP,
        PORTA_TELEMETRIA,
    )
except ImportError:  # Execucao direta: python analise_Bytes_Controle/demonstracaoTCP.py
    from protocolo_telemetria import (
        ConexaoEncerrada,
        FrameInvalido,
        LeitorFramesTCP,
        PORTA_TELEMETRIA,
    )


def ler_argumentos():
    parser = argparse.ArgumentParser(
        description="Exibe a telemetria HID recebida na porta TCP 8081."
    )
    parser.add_argument("ip", nargs="?", help="IP mostrado pela ESP32 no Serial Monitor")
    parser.add_argument("--porta", type=int, default=PORTA_TELEMETRIA)
    parser.add_argument(
        "--todos",
        action="store_true",
        help="Exibe tambem payloads sem botao mapeado pressionado.",
    )
    parser.add_argument("--reconectar-em", type=float, default=2.0)
    argumentos = parser.parse_args()

    if not argumentos.ip:
        argumentos.ip = input("IP da ESP32: ").strip()
    if not argumentos.ip:
        parser.error("Informe o IP da ESP32.")
    if argumentos.reconectar_em < 0:
        parser.error("--reconectar-em nao pode ser negativo.")

    return argumentos


def decodificar_acao(payload):
    """Decodifica as mascaras observadas no byte 13 do report Xbox usado."""
    if len(payload) < 14:
        return "PACOTE CURTO: nao possui byte 13"

    estado_botoes = payload[13]
    # O freio explicito tem prioridade caso mais de uma mascara esteja ativa.
    if estado_botoes & 0x02:
        return "PARAR (botao B)"
    if estado_botoes & 0x08:
        return "FRENTE (botao X)"
    if estado_botoes & 0x10:
        return "TRAS (botao Y)"
    if estado_botoes == 0:
        return "SEM COMANDO MAPEADO"
    return "SEM COMANDO MAPEADO (byte 13 = 0x{:02X})".format(estado_botoes)


def exibir_telemetria(argumentos):
    print("==================================================")
    print("       ROVER IHS - TELEMETRIA HID VIA TCP")
    print("==================================================")
    print("[*] Destino: {}:{} (Ctrl+C para encerrar)".format(argumentos.ip, argumentos.porta))

    try:
        while True:
            try:
                print("[*] Conectando a telemetria...")
                with socket.create_connection((argumentos.ip, argumentos.porta), timeout=5) as conexao:
                    conexao.settimeout(None)
                    leitor = LeitorFramesTCP(conexao)
                    print("[*] Conexao estabelecida. Aguardando frames HID.\n")

                    while True:
                        payload = leitor.receber_pacote()
                        acao = decodificar_acao(payload)
                        if not argumentos.todos and acao == "SEM COMANDO MAPEADO":
                            continue

                        hex_str = " ".join("{:02X}".format(byte) for byte in payload)
                        print("RAW ({} bytes): [{}]".format(len(payload), hex_str))
                        print("--> DECODED: {}\n".format(acao))

            except FrameInvalido as erro:
                # Depois de um tamanho invalido, a fronteira do fluxo e desconhecida.
                print("[!] {} Frame descartado; sera feita uma nova conexao.".format(erro))
            except (OSError, ConexaoEncerrada) as erro:
                print("[!] Conexao de telemetria encerrada: {}".format(erro))

            print("[*] Nova tentativa em {} s...".format(argumentos.reconectar_em))
            time.sleep(argumentos.reconectar_em)
    except KeyboardInterrupt:
        print("\n[*] Telemetria encerrada pelo usuario.")


if __name__ == "__main__":
    exibir_telemetria(ler_argumentos())
