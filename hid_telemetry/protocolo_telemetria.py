"""Leitura do protocolo de telemetria TCP do Rover.

Cada mensagem no fluxo TCP e formada por um cabecalho de dois bytes em
big-endian, seguido pelo payload HID bruto. O leitor conserva os bytes que
chegam juntos no mesmo ``recv``, pois TCP nao preserva fronteiras de pacote.
"""

import socket


PORTA_TELEMETRIA = 8081
TAMANHO_CABECALHO = 2
MAX_TAMANHO_PACOTE = 512


class ConexaoEncerrada(ConnectionError):
    """A outra ponta fechou a conexao antes de completar um frame."""


class FrameInvalido(ValueError):
    """O cabecalho recebido nao representa um payload HID valido."""


class LeitorFramesTCP:
    """Le frames consecutivos sem assumir como o TCP os fragmentou."""

    def __init__(self, conexao):
        self._conexao = conexao
        self._buffer = bytearray()

    def receber_exatamente(self, tamanho):
        """Retorna exatamente ``tamanho`` bytes, preservando sobras no buffer."""
        if tamanho < 0:
            raise ValueError("O tamanho solicitado nao pode ser negativo.")

        while len(self._buffer) < tamanho:
            bloco = self._conexao.recv(4096)
            if not bloco:
                raise ConexaoEncerrada(
                    "Conexao encerrada antes de completar o frame de telemetria."
                )
            self._buffer.extend(bloco)

        dados = bytes(self._buffer[:tamanho])
        del self._buffer[:tamanho]
        return dados

    def receber_pacote(self):
        """Le um frame ``[uint16 big-endian][payload HID]``."""
        cabecalho = self.receber_exatamente(TAMANHO_CABECALHO)
        tamanho = int.from_bytes(cabecalho, byteorder="big")

        if tamanho == 0 or tamanho > MAX_TAMANHO_PACOTE:
            raise FrameInvalido(
                "Tamanho de payload invalido: {} bytes (limite: {}).".format(
                    tamanho, MAX_TAMANHO_PACOTE
                )
            )

        return self.receber_exatamente(tamanho)
