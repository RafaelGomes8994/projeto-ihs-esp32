"""Analisa capturas de payload HID salvas por ``xbox_sniff.py``."""

import argparse
from collections import Counter
from pathlib import Path

try:
    from .protocolo_telemetria import MAX_TAMANHO_PACOTE
except ImportError:  # Execucao direta: python analise_Bytes_Controle/analisar_dump.py
    from protocolo_telemetria import MAX_TAMANHO_PACOTE


TAMANHO_MINIMO_HID = 14
ARQUIVO_PADRAO = Path(__file__).with_name("xbox_dump.txt")


def ler_argumentos():
    parser = argparse.ArgumentParser(
        description="Compara bytes entre payloads HID salvos em texto hexadecimal."
    )
    parser.add_argument("arquivo", nargs="?", type=Path, default=ARQUIVO_PADRAO)
    parser.add_argument(
        "--detalhado",
        action="store_true",
        help="Mostra as diferencas de cada amostra, alem do resumo agregado.",
    )
    return parser.parse_args()


def carregar_amostras(arquivo):
    amostras = []
    descartadas = []
    curtas = []
    linhas_vazias = 0

    with arquivo.open("r", encoding="utf-8") as fonte:
        for numero_linha, linha in enumerate(fonte, start=1):
            texto = linha.strip()
            if not texto:
                linhas_vazias += 1
                continue

            try:
                dados = bytes.fromhex(texto)
            except ValueError as erro:
                descartadas.append((numero_linha, "hexadecimal invalido: {}".format(erro)))
                continue

            if not dados:
                descartadas.append((numero_linha, "payload vazio"))
            elif len(dados) > MAX_TAMANHO_PACOTE:
                descartadas.append(
                    (
                        numero_linha,
                        "payload com {} bytes (limite: {})".format(
                            len(dados), MAX_TAMANHO_PACOTE
                        ),
                    )
                )
            elif len(dados) < TAMANHO_MINIMO_HID:
                curtas.append((numero_linha, len(dados)))
            else:
                amostras.append((numero_linha, dados))

    return amostras, descartadas, curtas, linhas_vazias


def diferencas(referencia, atual):
    resultado = []
    for indice, (antes, depois) in enumerate(zip(referencia, atual)):
        if antes != depois:
            resultado.append((indice, antes, depois))
    if len(referencia) != len(atual):
        resultado.append(("tamanho", len(referencia), len(atual)))
    return resultado


def analisar(arquivo, detalhado=False):
    amostras, descartadas, curtas, linhas_vazias = carregar_amostras(arquivo)

    print("Arquivo: {}".format(arquivo))
    print("Pacotes HID analisaveis: {}".format(len(amostras)))
    if linhas_vazias:
        print("Linhas vazias ignoradas: {}".format(linhas_vazias))
    if curtas:
        print(
            "Pacotes curtos ignorados (menos de {} bytes): {}".format(
                TAMANHO_MINIMO_HID, len(curtas)
            )
        )
    if descartadas:
        print("Linhas invalidas descartadas: {}".format(len(descartadas)))
        for numero_linha, motivo in descartadas[:10]:
            print("  - linha {}: {}".format(numero_linha, motivo))
        if len(descartadas) > 10:
            print("  - ... e mais {} linha(s).".format(len(descartadas) - 10))

    if not amostras:
        print("Nenhum payload HID valido foi encontrado.")
        return 1

    linha_referencia, referencia = amostras[0]
    print(
        "Referencia: linha {} ({} bytes): {}".format(
            linha_referencia, len(referencia), referencia.hex(" ")
        )
    )

    mudancas = Counter()
    for numero_linha, atual in amostras[1:]:
        alteracoes = diferencas(referencia, atual)
        for alteracao in alteracoes:
            mudancas[alteracao] += 1

        if detalhado and alteracoes:
            texto = []
            for indice, antes, depois in alteracoes:
                if indice == "tamanho":
                    texto.append("tamanho: {} -> {}".format(antes, depois))
                else:
                    texto.append("Byte[{}]: {:02X} -> {:02X}".format(indice, antes, depois))
            print("Linha {} mudou: {}".format(numero_linha, "; ".join(texto)))

    if not mudancas:
        print("Nenhuma diferenca em relacao a referencia.")
        return 0

    print("Mudancas agregadas em relacao a referencia:")
    def chave_ordenacao(item):
        indice, antes, depois = item[0]
        if indice == "tamanho":
            return (1, 0, antes, depois)
        return (0, indice, antes, depois)

    for alteracao, ocorrencias in sorted(mudancas.items(), key=chave_ordenacao):
        indice, antes, depois = alteracao
        if indice == "tamanho":
            descricao = "tamanho: {} -> {}".format(antes, depois)
        else:
            descricao = "Byte[{}]: {:02X} -> {:02X}".format(indice, antes, depois)
        print("  - {} ({} ocorrencia(s))".format(descricao, ocorrencias))

    return 0


if __name__ == "__main__":
    argumentos = ler_argumentos()
    raise SystemExit(analisar(argumentos.arquivo, argumentos.detalhado))
