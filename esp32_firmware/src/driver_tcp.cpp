#include "driver_tcp.h"
#include "driver_motores.h"

WiFiServer server(8080);
extern QueueHandle_t filaComandos;

void inicializarWiFi(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);

    // Timeout de 15 segundos — se a rede não responder, continua sem WiFi.
    // O rover ainda opera normalmente via BLE (controle Xbox).
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
        delay(500);
        Serial.print(".");
        tentativas++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[TCP] Wi-Fi conectado! IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n[TCP] Wi-Fi indisponível — operando só via BLE.");
    }
}

void iniciarServidorTCP() {
    if (WiFi.status() != WL_CONNECTED) return;
    server.begin();
    Serial.println("[TCP] Servidor TCP iniciado na porta 8080.");
}

void verificarConexoesTCP() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClient client = server.available();
    if (client) {
        if (client.available()) {
            char comando = client.read();
            Direcao dir = PARAR;
            switch (comando) {
                case 'F': dir = FRENTE; break;
                case 'T': dir = TRAS;   break;
                default:  dir = PARAR;  break;
            }
            xQueueSend(filaComandos, &dir, 0);
            client.println("OK");
        }
        client.stop();
    }
}