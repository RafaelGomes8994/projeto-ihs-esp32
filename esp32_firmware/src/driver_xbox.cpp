// =============================================================================
// driver_xbox.cpp - BLE/HID Xbox com reconexao sem vazamento e heartbeat
// =============================================================================

#include "driver_xbox.h"

#include <BLESecurity.h>

#include "driver_motores.h"
#include "driver_tcp.h"

namespace {

constexpr uint32_t TEMPO_MAXIMO_AUTENTICACAO_MS = 5000;

volatile EstadoBLE_t estadoAtual = EstadoBLE_t::CONECTANDO;
volatile bool segurancaEstabelecida = false;

// Um unico cliente e um unico conjunto de callbacks vivem durante todo o boot.
// Assim reconectar nao cria BLEClient/BLESecurity/callbacks novos a cada falha.
BLEClient* clienteBLE = nullptr;
BLERemoteCharacteristic* caracteristicaHID = nullptr;
BLESecurity segurancaBLE;

portMUX_TYPE muxComandoBLE = portMUX_INITIALIZER_UNLOCKED;
Direcao comandoBLEAtual = Direcao::PARAR;
uint32_t ultimoHeartbeatBLEMs = 0;

bool passouDoTempo(uint32_t agora, uint32_t referencia, uint32_t intervalo) {
    return static_cast<uint32_t>(agora - referencia) >= intervalo;
}

Direcao obterComandoBLE() {
    Direcao comando;
    portENTER_CRITICAL(&muxComandoBLE);
    comando = comandoBLEAtual;
    portEXIT_CRITICAL(&muxComandoBLE);
    return comando;
}

void atualizarComandoBLE(Direcao comando, bool forcarEnvio) {
    bool mudou = false;
    portENTER_CRITICAL(&muxComandoBLE);
    mudou = comandoBLEAtual != comando;
    comandoBLEAtual = comando;
    portEXIT_CRITICAL(&muxComandoBLE);

    // P vindo de B/desconexao e uma parada explicita. Um report ocioso que ja
    // estava em PARAR nao fica parando repetidamente um controlador TCP ativo.
    if (mudou || forcarEnvio) {
        enviarComandoMotor(comando, FonteComando::BLE);
    }
}

class CallbackSeguranca final : public BLESecurityCallbacks {
public:
    bool onConfirmPIN(uint32_t pin) override {
        (void)pin;
        return true;
    }

    uint32_t onPassKeyRequest() override {
        return 0;
    }

    void onPassKeyNotify(uint32_t passKey) override {
        (void)passKey;
    }

    bool onSecurityRequest() override {
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t authCmpl) override {
        segurancaEstabelecida = authCmpl.success;
        if (authCmpl.success) {
            Serial.println("[XBOX BLE] Bonding concluido.");
        } else {
            Serial.printf("[XBOX BLE] Falha de autenticacao: 0x%02X\n",
                          static_cast<unsigned>(authCmpl.fail_reason));
        }
    }
};

class CallbackClienteXbox final : public BLEClientCallbacks {
public:
    void onConnect(BLEClient* cliente) override {
        (void)cliente;
        Serial.println("[XBOX BLE] Conectado fisicamente.");
    }

    void onDisconnect(BLEClient* cliente) override {
        (void)cliente;
        caracteristicaHID = nullptr;
        segurancaEstabelecida = false;
        estadoAtual = EstadoBLE_t::DESCONECTADO;
        atualizarComandoBLE(Direcao::PARAR, true);
        Serial.println("[XBOX BLE] Desconectado; PARAR enviado e reconexao agendada.");
    }
};

CallbackSeguranca callbackSeguranca;
CallbackClienteXbox callbackCliente;

bool garantirClienteBLE() {
    if (clienteBLE != nullptr) {
        return true;
    }

    clienteBLE = BLEDevice::createClient();
    if (clienteBLE == nullptr) {
        Serial.println("[XBOX BLE] ERRO: nao foi possivel criar BLEClient.");
        return false;
    }

    clienteBLE->setClientCallbacks(&callbackCliente);
    return true;
}

void desconectarSeNecessario() {
    if (clienteBLE != nullptr && clienteBLE->isConnected()) {
        clienteBLE->disconnect();
    }
}

bool conectarESubscrever() {
    if (!garantirClienteBLE()) {
        return false;
    }

    caracteristicaHID = nullptr;
    segurancaEstabelecida = false;
    const BLEAddress enderecoXbox(MAC_XBOX);

    if (clienteBLE->isConnected()) {
        desconectarSeNecessario();
        return false;
    }

    Serial.printf("[XBOX BLE] Conectando ao MAC %s...\n", MAC_XBOX);
    if (!clienteBLE->connect(enderecoXbox)) {
        Serial.println("[XBOX BLE] Falha na conexao.");
        return false;
    }

    const uint32_t inicioAutenticacao = millis();
    while (!segurancaEstabelecida && clienteBLE->isConnected() &&
           !passouDoTempo(millis(), inicioAutenticacao,
                           TEMPO_MAXIMO_AUTENTICACAO_MS)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!clienteBLE->isConnected()) {
        return false;
    }

    if (!segurancaEstabelecida) {
        // Alguns controles reutilizam bond ja existente e nao disparam novo
        // callback de autenticacao. O acesso ao HID decide se pode continuar.
        Serial.println("[XBOX BLE] Autenticacao sem callback; verificando HID.");
    }

    BLERemoteService* servicoHID =
        clienteBLE->getService(BLEUUID(UUID_SERVICO_HID));
    if (servicoHID == nullptr) {
        Serial.println("[XBOX BLE] Servico HID nao encontrado.");
        desconectarSeNecessario();
        return false;
    }

    // O HID pode expor mais de uma caracteristica 0x2A4D. Escolhemos
    // explicitamente aquela que suporta notificacoes, em vez de depender de
    // getCharacteristic() retornar a instancia certa.
    const auto* caracteristicas = servicoHID->getCharacteristics();
    if (caracteristicas != nullptr) {
        for (const auto& item : *caracteristicas) {
            BLERemoteCharacteristic* candidata = item.second;
            if (candidata != nullptr &&
                candidata->getUUID().equals(BLEUUID(UUID_CARACTERISTICA_HID)) &&
                candidata->canNotify()) {
                caracteristicaHID = candidata;
                break;
            }
        }
    }

    if (caracteristicaHID == nullptr) {
        Serial.println("[XBOX BLE] Input Report com Notify nao encontrado.");
        desconectarSeNecessario();
        return false;
    }

    caracteristicaHID->registerForNotify(
        [](BLERemoteCharacteristic* caracteristica, uint8_t* dados,
           size_t tamanho, bool notificacao) {
            (void)caracteristica;
            if (!notificacao || dados == nullptr || tamanho == 0) {
                return;
            }

            // O callback BLE nunca toca em WiFiClient. Ele apenas enfileira o
            // pacote e atualiza o estado de comando em memoria.
            enfileirarTelemetria(dados, tamanho);

            if (tamanho < 14) {
                return;
            }

            const uint8_t botoes = dados[13];
            if (botoes & 0x02) {          // B: parada/freio explicito
                atualizarComandoBLE(Direcao::PARAR, true);
            } else if (botoes & 0x08) {   // X: frente
                atualizarComandoBLE(Direcao::FRENTE, false);
            } else if (botoes & 0x10) {   // Y: tras
                atualizarComandoBLE(Direcao::TRAS, false);
            } else {                      // soltar X/Y: para uma unica vez
                atualizarComandoBLE(Direcao::PARAR, false);
            }
        });

    Serial.println("[XBOX BLE] HID subscrito; comando pronto.");
    return true;
}

}  // namespace

void inicializarBLE() {
    BLEDevice::init("Rover-ESP32");
    BLEDevice::setMTU(512);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

    segurancaBLE.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    segurancaBLE.setCapability(ESP_IO_CAP_NONE);
    segurancaBLE.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    segurancaBLE.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLEDevice::setSecurityCallbacks(&callbackSeguranca);

    Serial.println("[XBOX BLE] Pilha BLE inicializada.");
}

void TaskXbox(void* parametros) {
    (void)parametros;
    estadoAtual = EstadoBLE_t::CONECTANDO;

    for (;;) {
        switch (estadoAtual) {
            case EstadoBLE_t::CONECTANDO:
                if (conectarESubscrever()) {
                    ultimoHeartbeatBLEMs = millis();
                    estadoAtual = EstadoBLE_t::CONECTADO;
                } else {
                    estadoAtual = EstadoBLE_t::DESCONECTADO;
                }
                break;

            case EstadoBLE_t::CONECTADO: {
                if (clienteBLE == nullptr || !clienteBLE->isConnected()) {
                    atualizarComandoBLE(Direcao::PARAR, true);
                    estadoAtual = EstadoBLE_t::DESCONECTADO;
                    break;
                }

                const Direcao comando = obterComandoBLE();
                const uint32_t agora = millis();
                if (comando != Direcao::PARAR &&
                    passouDoTempo(agora, ultimoHeartbeatBLEMs,
                                  INTERVALO_HEARTBEAT_BLE_MS)) {
                    // Mantem a concessao BLE viva enquanto X/Y estiver
                    // pressionado, mesmo que o controle nao envie novo report.
                    enviarComandoMotor(comando, FonteComando::BLE);
                    ultimoHeartbeatBLEMs = agora;
                }

                vTaskDelay(pdMS_TO_TICKS(20));
                break;
            }

            case EstadoBLE_t::DESCONECTADO:
                // onDisconnect ja enviou PARAR. Quando uma tentativa falha
                // antes de conectar, nao interferimos em um controlador TCP.
                vTaskDelay(pdMS_TO_TICKS(INTERVALO_RECONEXAO_MS));
                estadoAtual = EstadoBLE_t::CONECTANDO;
                break;
        }
    }
}
