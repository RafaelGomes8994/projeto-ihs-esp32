"""Captura payloads HID encaminhados pela ESP32 na porta de telemetria."""

import argparse
from pathlib import Path
import socket
import time

try:
    from .protocolo_telemetria import (
        ConexaoEncerrada,
        FrameInvalido,
        LeitorFramesTCP,
        PORTA_TELEMETRIA,
    )
except ImportError:  # Execucao direta: python analise_Bytes_Controle/xbox_sniff.py
    from protocolo_telemetria import (
        ConexaoEncerrada,
        FrameInvalido,
        LeitorFramesTCP,
        PORTA_TELEMETRIA,
    )


ARQUIVO_PADRAO = Path(__file__).with_name("captura_xbox.txt")


def ler_argumentos():
    parser = argparse.ArgumentParser(
        description="Grava payloads HID da telemetria TCP da ESP32."
    )
    parser.add_argument("ip", nargs="?", help="IP mostrado pela ESP32 no Serial Monitor")
    parser.add_argument("--porta", type=int, default=PORTA_TELEMETRIA)
    parser.add_argument("--arquivo", type=Path, default=ARQUIVO_PADRAO)
    parser.add_argument(
        "--append",
        action="store_true",
        help="Acrescenta a captura ao arquivo. Sem esta opcao, cria uma captura nova.",
    )
    parser.add_argument(
        "--reconectar-em",
        type=float,
        default=2.0,
        help="Segundos de espera antes de tentar reconectar (padrao: 2).",
    )
    argumentos = parser.parse_args()

    if not argumentos.ip:
        argumentos.ip = input("IP da ESP32: ").strip()
    if not argumentos.ip:
        parser.error("Informe o IP da ESP32.")
    if argumentos.reconectar_em < 0:
        parser.error("--reconectar-em nao pode ser negativo.")

    return argumentos


def capturar(argumentos):
    modo = "a" if argumentos.append else "w"
    total = 0

    with argumentos.arquivo.open(modo, encoding="utf-8") as arquivo:
        print(
            "[*] Gravando payloads em {} (modo {}).".format(
                argumentos.arquivo, "append" if argumentos.append else "nova captura"
            )
        )
        print("[*] Telemetria: {}:{}; Ctrl+C para encerrar.".format(argumentos.ip, argumentos.porta))

        while True:
            try:
                print("[*] Conectando a {}:{}...".format(argumentos.ip, argumentos.porta))
                with socket.create_connection((argumentos.ip, argumentos.porta), timeout=5) as conexao:
                    # A conexao pode ficar ociosa enquanto nao ha notificacao HID.
                    conexao.settimeout(None)
                    leitor = LeitorFramesTCP(conexao)
                    print("[*] Conectado. Aguardando frames HID.")

                    while True:
                        payload = leitor.receber_pacote()
                        linha = payload.hex(" ")
                        arquivo.write(linha + "\n")
                        arquivo.flush()
                        total += 1
                        print("Capturado ({} bytes): {}".format(len(payload), linha))

            except FrameInvalido as erro:
                # Sem marcador de sincronizacao, o proximo frame nao e confiavel.
                print("[!] {} Frame descartado; a conexao sera refeita.".format(erro))
            except (OSError, ConexaoEncerrada) as erro:
                print("[!] Conexao de telemetria encerrada: {}".format(erro))
            except KeyboardInterrupt:
                print("\n[*] Captura encerrada. {} payload(s) gravado(s).".format(total))
                return

            print("[*] Nova tentativa em {} s...".format(argumentos.reconectar_em))
            try:
                time.sleep(argumentos.reconectar_em)
            except KeyboardInterrupt:
                print("\n[*] Captura encerrada. {} payload(s) gravado(s).".format(total))
                return


if __name__ == "__main__":
    capturar(ler_argumentos())
