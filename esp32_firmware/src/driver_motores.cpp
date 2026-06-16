// =============================================================================
// driver_motores.cpp — Implementação do Driver de Motores DC
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#include "driver_motores.h"

// =============================================================================
// Pinagem da Ponte H — ajuste conforme o seu cabeamento no ESP32-CAM
// Pinos disponíveis no ESP32-CAM (sem conflito com câmera e PSRAM):
//   GPIO 12, 13, 14, 15
//   EVITE: GPIO 0 (boot), 1/3 (UART), 4 (flash LED), 2/33-39 (câmera)
// =============================================================================
static const int pinoMotor1_F = 12;
static const int pinoMotor1_B = 13;
static const int pinoMotor2_F = 14;
static const int pinoMotor2_B = 15;

// =============================================================================
// Declaração da função Assembly (calculo_pwm.S)
// =============================================================================
extern "C" uint32_t calcularPWM(uint8_t entrada);

// CORREÇÃO: pwmValue era uma variável global inicializada com calcularPWM(200).
// Variáveis globais com inicializadores não-triviais (chamadas de função) são
// executadas ANTES do setup(), durante a fase de inicialização estática do C++.
// Chamar Assembly nesse momento é arriscado e desnecessário.
// Solução: inicializar dentro de inicializarMotores() como variável local estática.
static uint32_t pwmVelocidade = 0;

// =============================================================================
// inicializarMotores()
// =============================================================================
void inicializarMotores() {
    // Calcula o PWM aqui, dentro do setup(), onde o runtime já está pronto
    pwmVelocidade = calcularPWM(200);   // 200 * 4 = 800 (≈78% duty cycle)

    pinMode(pinoMotor1_F, OUTPUT);
    pinMode(pinoMotor1_B, OUTPUT);
    pinMode(pinoMotor2_F, OUTPUT);
    pinMode(pinoMotor2_B, OUTPUT);

    processarComandoMotor(PARAR);
    Serial.printf("[MOTORES] Inicializados. PWM calculado: %lu\n", pwmVelocidade);
}

// =============================================================================
// processarComandoMotor()
// =============================================================================
void processarComandoMotor(Direcao cmd) {
    switch (cmd) {
        case FRENTE:
            digitalWrite(pinoMotor1_F, HIGH); digitalWrite(pinoMotor1_B, LOW);
            digitalWrite(pinoMotor2_F, HIGH); digitalWrite(pinoMotor2_B, LOW);
            break;
        case TRAS:
            digitalWrite(pinoMotor1_F, LOW);  digitalWrite(pinoMotor1_B, HIGH);
            digitalWrite(pinoMotor2_F, LOW);  digitalWrite(pinoMotor2_B, HIGH);
            break;
        case PARAR:
        default:
            digitalWrite(pinoMotor1_F, LOW);  digitalWrite(pinoMotor1_B, LOW);
            digitalWrite(pinoMotor2_F, LOW);  digitalWrite(pinoMotor2_B, LOW);
            break;
    }
}

// =============================================================================
// TaskMotores() — Consumidor da fila de comandos (Core 1)
// =============================================================================
void TaskMotores(void* parametros) {
    Direcao cmd;
    Serial.println("[MOTORES] TaskMotores iniciada no Core 1.");

    for (;;) {
        if (xQueueReceive(filaComandos, &cmd, portMAX_DELAY) == pdTRUE) {
            processarComandoMotor(cmd);
        }
    }
    vTaskDelete(NULL);
}   