#include "driver_tcp.h"
#include "driver_motores.h"

WiFiServer server(8080);
WiFiClient activeClient; // Guardamos a conexão aqui


void inicializarWiFi(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[TCP] IP: " + WiFi.localIP().toString());
}

void iniciarServidorTCP() {
    server.begin();
    server.setNoDelay(true); // Fundamental para telemetria em tempo real
}

void verificarConexoesTCP() {
    // Se não houver cliente ativo, aceitamos uma nova conexão
    if (!activeClient || !activeClient.connected()) {
        activeClient = server.available();
    }
    
    // Se o cliente enviou comandos
    if (activeClient && activeClient.available()) {
        char comando = activeClient.read();
        Direcao dir = (comando == 'F') ? FRENTE : (comando == 'T' ? TRAS : PARAR);
        xQueueSend(filaComandos, &dir, 0);
    }
}

// NOVA FUNÇÃO: Envia dados do BLE para o cliente TCP
void encaminharParaTCP(uint8_t* dados, size_t tamanho) {
    if (activeClient && activeClient.connected()) {
        activeClient.write(dados, tamanho);
    }
}