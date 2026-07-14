// =============================================================================
// driver_motores.h - Controle seguro dos motores pela ponte H L9110S
// =============================================================================

#ifndef DRIVER_MOTORES_H
#define DRIVER_MOTORES_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// O rover aceita somente estes tres estados de movimento. Os valores explicitos
// tambem deixam os logs e o protocolo de depuracao estaveis.
enum class Direcao : uint8_t {
    FRENTE = 0,
    TRAS   = 1,
    PARAR  = 2
};

// Identifica quem produziu o comando. A TaskMotores usa esta informacao para
// impedir que BLE e TCP alternem a direcao a cada pacote recebido.
enum class FonteComando : uint8_t {
    SISTEMA = 0,
    TCP     = 1,
    BLE     = 2
};

// A fila guarda sempre o estado mais recente, nunca uma sequencia atrasada de
// movimentos. recebidoEmMs permite descartar comandos que envelheceram antes
// de a task poder processa-los.
struct ComandoMotor {
    Direcao direcao;
    FonteComando fonte;
    uint32_t recebidoEmMs;
};

// Definida em main.cpp. Deve ser criada com exatamente uma posicao, pois os
// produtores usam xQueueOverwrite para manter apenas o ultimo estado.
extern QueueHandle_t filaComandos;

// Tanto o driver TCP quanto o BLE enviam heartbeat a cada 100 ms. Sem um
// heartbeat valido por 500 ms, a TaskMotores para fisicamente o rover.
constexpr uint32_t TEMPO_LIMITE_COMANDO_MS = 500;

// Publica um comando sem bloquear. PARAR possui tratamento adicional para nao
// ser perdido caso um comando de movimento sobrescreva a fila logo em seguida.
bool enviarComandoMotor(Direcao direcao, FonteComando fonte);

// Configura LEDC e os pinos da ponte H, iniciando sempre em repouso.
void inicializarMotores();

// Aciona a ponte H. E chamado internamente pela TaskMotores; nao use esta
// funcao como atalho para contornar a arbitragem ou o watchdog.
void processarComandoMotor(Direcao cmd);

// Consumidor da fila e responsavel pelo watchdog e pela arbitragem das fontes.
void TaskMotores(void* parametros);

#endif  // DRIVER_MOTORES_H
