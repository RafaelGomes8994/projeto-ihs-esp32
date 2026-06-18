// =============================================================================
// driver_motores.cpp — Implementação do Driver de Motores DC (Ponte H L9110S)
// =============================================================================

#include "driver_motores.h"

// Pinagem (Evitando conflitos com a câmera/flash do ESP32-CAM)
static const int pinoMotor1_F = 12; // Direção Frente - Motor Esq
static const int pinoMotor1_B = 13; // Direção Trás - Motor Esq
static const int pinoMotor2_F = 15; // Direção Frente - Motor Dir
static const int pinoMotor2_B = 14; // Direção Trás - Motor Dir

// Declaração da função Assembly (calculo_pwm.S)
extern "C" uint32_t calcularPWM(uint8_t entrada);

static uint32_t pwmVelocidade = 0;

void inicializarMotores() {
    // Calculamos o PWM (Mesmo não usando agora, validamos a função Assembly)
    pwmVelocidade = calcularPWM(50);   // 200 * 4 = 800

    pinMode(pinoMotor1_F, OUTPUT);
    pinMode(pinoMotor1_B, OUTPUT);
    pinMode(pinoMotor2_F, OUTPUT);
    pinMode(pinoMotor2_B, OUTPUT);

    // Garante que o robô liga completamente parado e seguro
    processarComandoMotor(PARAR);
    Serial.printf("[MOTORES] Inicializados. Hardware configurado (L9110S).\n");
}

void processarComandoMotor(Direcao cmd) {
    // Para segurança na L9110S: Sempre forçamos TUDO a LOW antes de mudar de direção.
    // Isso evita curto-circuitos instantâneos e picos de corrente na bateria.
    digitalWrite(pinoMotor1_F, LOW);
    digitalWrite(pinoMotor1_B, LOW);
    digitalWrite(pinoMotor2_F, LOW);
    digitalWrite(pinoMotor2_B, LOW);
    
    // Pequeno atraso para dar tempo aos transístores internos descarregarem
    delay(10); 

    switch (cmd) {
        case FRENTE:
            digitalWrite(pinoMotor1_F, HIGH); 
            digitalWrite(pinoMotor2_F, HIGH); 
            break;
        case TRAS:
            digitalWrite(pinoMotor1_B, HIGH);
            digitalWrite(pinoMotor2_B, HIGH);
            break;
        case PARAR:
        default:
            // Já colocámos a LOW acima. Fica parado em repouso livre.
            break;
    }
}

// =============================================================================
// TaskMotores() — Consumidor da fila de comandos (Core 1)
// =============================================================================
void TaskMotores(void *pvParameters) {
    Direcao cmd;
    Serial.println("[MOTORES] TaskMotores iniciada no Core 1.");
    
    while(1) {
        // Bloqueia a task até existir um comando pendente na fila
        if (xQueueReceive(filaComandos, &cmd, portMAX_DELAY)) {
            Serial.printf("[MOTORES] Comando lógico (%d) retirado da fila.\n", cmd);
            
            // Invoca a lógica da ponte H
            processarComandoMotor(cmd);
        }
    }
}