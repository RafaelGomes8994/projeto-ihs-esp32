// =============================================================================
// main.cpp - Rover: TCP seguro, telemetria separada e controle BLE
// =============================================================================

#include <Arduino.h>

#include "driver_motores.h"
#include "driver_tcp.h"
#include "driver_xbox.h"
#include "wifi_config.h"

QueueHandle_t filaComandos = nullptr;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[MAIN] Rover iniciando (TCP + BLE com fail-safe)...");

    // A fila unitario/overwrite representa estado, nao uma lista de movimentos.
    filaComandos = xQueueCreate(1, sizeof(ComandoMotor));
    if (filaComandos == nullptr) {
        Serial.println("[MAIN] ERRO FATAL: falha ao criar fila de comandos.");
        while (true) {
            delay(1000);
        }
    }

    inicializarMotores();

    // O watchdog de motores deve existir antes de qualquer produtor de comando.
    if (xTaskCreatePinnedToCore(TaskMotores, "Motores", 3072, nullptr, 3,
                                nullptr, 1) != pdPASS) {
        Serial.println("[MAIN] ERRO FATAL: nao foi possivel criar TaskMotores.");
        while (true) {
            delay(1000);
        }
    }

    // Wi-Fi e iniciado sem espera bloqueante. Se a rede estiver indisponivel,
    // verificarConexoesTCP() reconecta em segundo plano e o BLE segue ativo.
    inicializarWiFi(ROVER_WIFI_SSID, ROVER_WIFI_PASSWORD);
    if (!iniciarServidorTCP()) {
        Serial.println("[MAIN] Telemetria indisponivel: fila TCP nao criada.");
    }

    inicializarBLE();
    if (xTaskCreatePinnedToCore(TaskXbox, "XboxBLE", 8192, nullptr, 1,
                                nullptr, 0) != pdPASS) {
        Serial.println("[MAIN] ERRO: Task Xbox nao criada; TCP permanece disponivel.");
    }

    Serial.println("[MAIN] Pronto: comandos TCP 8080, telemetria TCP 8081 e BLE.");
}

void loop() {
    // Este e o unico contexto que acessa WiFiClient. Callbacks BLE so usam filas.
    verificarConexoesTCP();
    delay(5);
}
