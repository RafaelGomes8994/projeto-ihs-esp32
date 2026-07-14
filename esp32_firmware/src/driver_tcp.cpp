// =============================================================================
// driver_tcp.cpp - TCP separado para comandos e telemetria BLE
// =============================================================================

#include "driver_tcp.h"

#include <cstring>

#include "driver_motores.h"

namespace {

constexpr uint32_t TEMPO_BOOT_WIFI_MS = 10000;
constexpr uint32_t INTERVALO_TENTATIVA_WIFI_MS = 5000;
constexpr UBaseType_t PROFUNDIDADE_FILA_TELEMETRIA = 4;
constexpr size_t MAX_COMANDOS_POR_LOOP = 32;

struct PacoteTelemetria {
    uint16_t tamanho;
    uint8_t dados[TAMANHO_MAX_PACOTE_TELEMETRIA];
};

WiFiServer servidorComandos(PORTA_TCP_COMANDOS);
WiFiServer servidorTelemetria(PORTA_TCP_TELEMETRIA);
WiFiClient clienteComandos;
WiFiClient clienteTelemetria;
QueueHandle_t filaTelemetria = nullptr;

const char* wifiSsid = nullptr;
const char* wifiPassword = nullptr;
uint32_t inicioBootWiFiMs = 0;
uint32_t ultimaTentativaWiFiMs = 0;
bool wifiConfigurado = false;
bool wifiEstavaConectado = false;
bool timeoutBootReportado = false;
bool servidoresIniciados = false;
bool clienteComandosAtivo = false;

bool passouDoTempo(uint32_t agora, uint32_t referencia, uint32_t intervalo) {
    return static_cast<uint32_t>(agora - referencia) >= intervalo;
}

void iniciarTentativaWiFi() {
    if (!wifiConfigurado) {
        return;
    }

    ultimaTentativaWiFiMs = millis();
    WiFi.begin(wifiSsid, wifiPassword);
    Serial.println("[WIFI] Tentativa de conexao iniciada.");
}

void fecharClienteComandos(bool emitirParada) {
    const bool haviaCliente = clienteComandosAtivo;
    if (clienteComandos) {
        clienteComandos.stop();
    }
    clienteComandosAtivo = false;

    if (emitirParada && haviaCliente) {
        enviarComandoMotor(Direcao::PARAR, FonteComando::TCP);
        Serial.println("[TCP] Cliente de comandos desconectado; PARAR enviado.");
    }
}

void fecharClienteTelemetria() {
    if (clienteTelemetria) {
        clienteTelemetria.stop();
    }
}

void iniciarServidoresSeNecessario() {
    if (servidoresIniciados || WiFi.status() != WL_CONNECTED) {
        return;
    }

    servidorComandos.begin();
    servidorComandos.setNoDelay(true);
    servidorTelemetria.begin();
    servidorTelemetria.setNoDelay(true);
    servidoresIniciados = true;

    Serial.printf("[TCP] Comandos em %u; telemetria em %u.\n",
                  PORTA_TCP_COMANDOS, PORTA_TCP_TELEMETRIA);
}

void encerrarServidoresPorQuedaWiFi() {
    fecharClienteComandos(true);
    fecharClienteTelemetria();
    if (servidoresIniciados) {
        servidorComandos.end();
        servidorTelemetria.end();
        servidoresIniciados = false;
    }
}

void manterWiFi() {
    const uint32_t agora = millis();
    const bool conectado = WiFi.status() == WL_CONNECTED;

    if (conectado) {
        if (!wifiEstavaConectado) {
            wifiEstavaConectado = true;
            timeoutBootReportado = false;
            Serial.println("[WIFI] Conectado. IP: " + WiFi.localIP().toString());
        }
        iniciarServidoresSeNecessario();
        return;
    }

    if (wifiEstavaConectado) {
        wifiEstavaConectado = false;
        Serial.println("[WIFI] Conexao perdida; mantendo BLE e tentando reconectar.");
        encerrarServidoresPorQuedaWiFi();
    }

    if (wifiConfigurado && !timeoutBootReportado &&
        passouDoTempo(agora, inicioBootWiFiMs, TEMPO_BOOT_WIFI_MS)) {
        timeoutBootReportado = true;
        Serial.println("[WIFI] Timeout de boot; BLE continua ativo e a reconexao segue em segundo plano.");
    }

    if (wifiConfigurado &&
        passouDoTempo(agora, ultimaTentativaWiFiMs, INTERVALO_TENTATIVA_WIFI_MS)) {
        iniciarTentativaWiFi();
    }
}

bool decodificarComando(uint8_t byte, Direcao& direcao) {
    switch (byte) {
        case 'F':
            direcao = Direcao::FRENTE;
            return true;
        case 'T':
            direcao = Direcao::TRAS;
            return true;
        case 'P':
            direcao = Direcao::PARAR;
            return true;
        default:
            return false;
    }
}

void aceitarClienteComandos() {
    if (clienteComandos && !clienteComandos.connected()) {
        fecharClienteComandos(true);
    }

    if (!clienteComandosAtivo) {
        WiFiClient candidato = servidorComandos.available();
        if (candidato) {
            clienteComandos = candidato;
            clienteComandos.setNoDelay(true);
            clienteComandosAtivo = true;
            Serial.println("[TCP] Cliente de comandos conectado.");
        }
    } else {
        // Mantem um unico controlador ativo; conexoes adicionais nao roubam o
        // comando de quem ja possui o socket.
        WiFiClient candidatoExtra = servidorComandos.available();
        if (candidatoExtra) {
            candidatoExtra.stop();
        }
    }
}

void lerComandosTCP() {
    if (!clienteComandosAtivo || !clienteComandos.connected()) {
        return;
    }

    size_t processados = 0;
    while (clienteComandos.available() > 0 && processados < MAX_COMANDOS_POR_LOOP) {
        const int recebido = clienteComandos.read();
        if (recebido < 0) {
            break;
        }

        Direcao direcao = Direcao::PARAR;
        if (decodificarComando(static_cast<uint8_t>(recebido), direcao)) {
            enviarComandoMotor(direcao, FonteComando::TCP);
        }
        ++processados;
    }

    if (!clienteComandos.connected()) {
        fecharClienteComandos(true);
    }
}

void aceitarClienteTelemetria() {
    if (clienteTelemetria && !clienteTelemetria.connected()) {
        fecharClienteTelemetria();
    }

    if (!clienteTelemetria || !clienteTelemetria.connected()) {
        WiFiClient candidato = servidorTelemetria.available();
        if (candidato) {
            clienteTelemetria = candidato;
            clienteTelemetria.setNoDelay(true);
            Serial.println("[TCP] Cliente de telemetria conectado.");
        }
    } else {
        WiFiClient candidatoExtra = servidorTelemetria.available();
        if (candidatoExtra) {
            candidatoExtra.stop();
        }
    }
}

bool enviarFrameTelemetria(const PacoteTelemetria& pacote) {
    if (!clienteTelemetria || !clienteTelemetria.connected()) {
        return false;
    }

    const uint8_t cabecalho[2] = {
        static_cast<uint8_t>(pacote.tamanho >> 8),
        static_cast<uint8_t>(pacote.tamanho & 0xFF)
    };

    // Se houver escrita parcial, fecha o stream em vez de iniciar o proximo
    // frame desalinhado. O cliente pode reconectar e retomar do proximo frame.
    if (clienteTelemetria.write(cabecalho, sizeof(cabecalho)) != sizeof(cabecalho) ||
        clienteTelemetria.write(pacote.dados, pacote.tamanho) != pacote.tamanho) {
        fecharClienteTelemetria();
        Serial.println("[TCP] Falha na telemetria; cliente encerrado.");
        return false;
    }

    return true;
}

void drenarTelemetria() {
    if (filaTelemetria == nullptr) {
        return;
    }

    PacoteTelemetria pacote{};
    while (xQueueReceive(filaTelemetria, &pacote, 0) == pdPASS) {
        // Sem consumidor, descartamos os dados antigos para que uma nova
        // conexao sempre comece com telemetria recente.
        if (!clienteTelemetria || !clienteTelemetria.connected()) {
            continue;
        }
        if (!enviarFrameTelemetria(pacote)) {
            break;
        }
    }
}

}  // namespace

void inicializarWiFi(const char* ssid, const char* password) {
    wifiSsid = ssid;
    wifiPassword = password;
    inicioBootWiFiMs = millis();
    ultimaTentativaWiFiMs = inicioBootWiFiMs;
    timeoutBootReportado = false;
    wifiEstavaConectado = false;

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    wifiConfigurado = wifiSsid != nullptr && wifiSsid[0] != '\0';
    if (!wifiConfigurado) {
        Serial.println("[WIFI] Sem credenciais locais; modo TCP desativado, BLE continua disponivel.");
        return;
    }

    iniciarTentativaWiFi();
}

bool iniciarServidorTCP() {
    if (filaTelemetria == nullptr) {
        filaTelemetria = xQueueCreate(PROFUNDIDADE_FILA_TELEMETRIA,
                                      sizeof(PacoteTelemetria));
    }

    if (filaTelemetria == nullptr) {
        Serial.println("[TCP] ERRO: nao foi possivel criar a fila de telemetria.");
        return false;
    }

    // Caso o Wi-Fi tenha conectado muito cedo, inicia os servidores ja aqui.
    iniciarServidoresSeNecessario();
    return true;
}

bool enfileirarTelemetria(const uint8_t* dados, size_t tamanho) {
    if (filaTelemetria == nullptr || dados == nullptr || tamanho == 0 ||
        tamanho > TAMANHO_MAX_PACOTE_TELEMETRIA) {
        return false;
    }

    PacoteTelemetria pacote{};
    pacote.tamanho = static_cast<uint16_t>(tamanho);
    std::memcpy(pacote.dados, dados, tamanho);

    // O callback BLE nunca espera por rede. Se o consumidor estiver atrasado,
    // descartamos este pacote em vez de bloquear a pilha Bluetooth.
    return xQueueSend(filaTelemetria, &pacote, 0) == pdPASS;
}

void verificarConexoesTCP() {
    manterWiFi();
    if (!servidoresIniciados) {
        return;
    }

    aceitarClienteComandos();
    lerComandosTCP();
    aceitarClienteTelemetria();
    drenarTelemetria();
}
