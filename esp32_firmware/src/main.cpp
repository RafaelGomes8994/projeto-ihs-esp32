// =============================================================================
// main.cpp — Arquitetura Híbrida: TCP Kernel + Xbox BLE Direto
// =============================================================================
#include <Arduino.h>
#include "driver_motores.h"
#include "driver_tcp.h"
#include "driver_xbox.h" 

QueueHandle_t filaComandos = nullptr;

// Substitua pelos dados da sua rede de 2.4GHz
static const char* WIFI_SSID     = "Zoro"; 
static const char* WIFI_PASSWORD = "Rafael@12";

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n[MAIN] Rover FPV iniciando (MODO HÍBRIDO TCP + BLE)...");

    // 1. Cria a fila de comandos (Protegida contra Condições de Corrida)
    filaComandos = xQueueCreate(10, sizeof(Direcao));
    if (filaComandos == nullptr) {
        Serial.println("[MAIN] ERRO FATAL: falha ao criar filaComandos!");
        while (true) { delay(1000); }
    }

    // 2. Inicializa os pinos físicos
    inicializarMotores();

    // 3. LIGA O WI-FI PRIMEIRO (Crucial para a Coexistência de Antena)
    Serial.println("[MAIN] Iniciando Wi-Fi...");
    inicializarWiFi(WIFI_SSID, WIFI_PASSWORD);
    iniciarServidorTCP();

    // 4. LIGA O BLUETOOTH DEPOIS
    Serial.println("[MAIN] Iniciando Bluetooth...");
    inicializarBLE();

    // 5. Inicia as Tasks do FreeRTOS
    // Task dos Motores roda no Core 1 (Prioridade alta para não engasgar)
    xTaskCreatePinnedToCore(TaskMotores, "Motores", 2048, NULL, 2, NULL, 1);
    
    // Task do Xbox BLE roda no Core 0 (Isolada do processamento principal)
    xTaskCreatePinnedToCore(TaskXbox, "XboxBLE", 8192, NULL, 1, NULL, 0);
    
    Serial.println("[MAIN] Setup concluído. Escutando PC (Rede) e Comando (BLE)...");
}

void loop() {
    // O Loop principal do Arduino fica encarregado de escutar a rede
    // Se chegar TCP, ele manda para a Fila. Se não chegar, passa reto.
    verificarConexoesTCP(); 
    delay(10);
}