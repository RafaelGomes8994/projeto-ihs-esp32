// =============================================================================
// driver_motores.h — Driver de Controle dos Motores DC (Ponte H L298N/L293D)
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#ifndef DRIVER_MOTORES_H
#define DRIVER_MOTORES_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =============================================================================
// Fila de comandos compartilhada entre produtores e consumidor:
//   Produtores: driver_tcp.cpp (comandos via rede) e driver_xbox.cpp (BLE)
//   Consumidor: TaskMotores() — lê e aciona os pinos da ponte H
//
// A variável é DEFINIDA em main.cpp (QueueHandle_t filaComandos;)
// e DECLARADA como extern aqui para todos os módulos que incluírem este header.
// =============================================================================
extern QueueHandle_t filaComandos;

// =============================================================================
// Enum de direções — vocabulário do robô
// =============================================================================
typedef enum {
    FRENTE,
    TRAS,
    PARAR
} Direcao;

// =============================================================================
// Protótipos das funções públicas
// =============================================================================

// Configura os pinos da ponte H como OUTPUT e para o robô.
// Chamar no setup() antes de criar as Tasks.
void inicializarMotores();

// Aciona os pinos da ponte H conforme a direção solicitada.
// Chamada internamente pela TaskMotores — não chamar diretamente do main.
void processarComandoMotor(Direcao cmd);

// Task FreeRTOS que consome a filaComandos e aciona os motores.
// Fixar no Core 1 com xTaskCreatePinnedToCore(..., 1).
void TaskMotores(void* parametros);

#endif // DRIVER_MOTORES_H