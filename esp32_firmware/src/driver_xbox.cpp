// =============================================================================
// driver_xbox.cpp — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#include "driver_xbox.h"
#include "driver_motores.h" // Necessário para a Fila e a Enum Direcao

static volatile EstadoBLE_t estadoAtual = XBOX_CONECTANDO;
static volatile bool segurancaEstabelecida = false;

static BLEClient* clienteBLE = nullptr;
static BLERemoteCharacteristic* caracteristicaHID = nullptr;

// =============================================================================
// Callback de Segurança BLE (Obrigatório para Comandos Xbox)
// =============================================================================
class CallbackSeguranca : public BLESecurityCallbacks {
    bool onConfirmPIN(uint32_t pin) override { return true; }
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t pass_key) override {}
    bool onSecurityRequest() override { return true; }
    
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
        if (auth_cmpl.success) {
            Serial.println("[XBOX BLE] Bonding concluído — controle autenticado!");
            segurancaEstabelecida = true;
        } else {
            Serial.printf("[XBOX BLE] Falha na autenticação. Código: 0x%02X\n", auth_cmpl.fail_reason);
            segurancaEstabelecida = false;
        }
    }
};

// =============================================================================
// Processamento dos Dados do Comando (O Mapeamento Real)
// =============================================================================
void processarDadosXbox(uint8_t* dados, size_t tamanho) {

#if MODO_SNIFFING == 1
    Serial.printf("[SNIFF %02d bytes] [ ", tamanho);
    for (size_t i = 0; i < tamanho; i++) {
        Serial.printf("%02X ", dados[i]);
    }
    Serial.println("]");
#endif

    // Proteção de memória: Garante que o pacote tem tamanho suficiente
    if (tamanho < 14) return;

    // A MÁGICA: O byte dos botões A, B, X, Y está exatamente no índice 13!
    uint8_t maskBotoes = dados[13];

    Direcao cmd = PARAR;

    // Decodificação via Bitwise AND
    // 0x08 = Botão X | 0x10 = Botão Y | 0x02 = Botão B
    if (maskBotoes & 0x08) {         
        cmd = FRENTE;
        Serial.println("[XBOX BLE] Botão X pressionado -> FRENTE");
    } 
    else if (maskBotoes & 0x10) {    
        cmd = TRAS;
        Serial.println("[XBOX BLE] Botão Y pressionado -> TRÁS");
    }
    else if (maskBotoes & 0x02) {    
        cmd = PARAR;
        Serial.println("[XBOX BLE] Botão B pressionado -> FREIO");
    }

    // Envia o comando para a fila dos motores de forma assíncrona
    if (filaComandos != nullptr) {
        if (cmd != PARAR) {
            xQueueSend(filaComandos, &cmd, 0);
        } else if (maskBotoes == 0 || (maskBotoes & 0x02)) {
            // Garante que o robô para se soltar tudo ou se carregar no B
            Direcao stopCmd = PARAR;
            xQueueSend(filaComandos, &stopCmd, 0);
        }
    }
}

// =============================================================================
// Callbacks de HID e Conexão
// =============================================================================
static void notificacaoHID(BLERemoteCharacteristic* pCarac, uint8_t* dados, size_t tamanho, bool eNotificacao) {
    if (!eNotificacao || dados == nullptr || tamanho == 0) return;
    processarDadosXbox(dados, tamanho);
}

class CallbackClienteXbox : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Conectado Fisicamente!");
    }
    void onDisconnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Desconectado. Reconectando...");
        segurancaEstabelecida = false;
        estadoAtual = XBOX_DESCONECTADO;
    }
};

// =============================================================================
// Máquina de Estados e Subscrição
// =============================================================================
static bool conectarESubscrever() {
    BLEAddress enderecoXbox(MAC_XBOX);

    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    clienteBLE = BLEDevice::createClient();
    clienteBLE->setClientCallbacks(new CallbackClienteXbox());

    Serial.printf("[XBOX BLE] Conectando ao MAC %s...\n", MAC_XBOX);
    if (!clienteBLE->connect(enderecoXbox)) {
        Serial.println("[XBOX BLE] Falha na conexão.");
        return false;
    }

    Serial.println("[XBOX BLE] Aguardando autenticação BLE...");
    int espera = 0;
    while (!segurancaEstabelecida && espera < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        espera++;
    }

    if (!segurancaEstabelecida) {
        Serial.println("[XBOX BLE] Timeout na autenticação. Tentando continuar...");
    }

    BLERemoteService* servicoHID = clienteBLE->getService(BLEUUID(UUID_SERVICO_HID));
    if (servicoHID == nullptr) {
        Serial.println("[XBOX BLE] Serviço HID não encontrado.");
        clienteBLE->disconnect();
        return false;
    }
    Serial.println("[XBOX BLE] Serviço HID encontrado.");

    auto* mapaCarac = servicoHID->getCharacteristics();
    caracteristicaHID = nullptr;

    for (auto& par : *mapaCarac) {
        BLERemoteCharacteristic* carac = par.second;
        if (carac->getUUID().equals(BLEUUID(UUID_CARACTERISTICA_HID)) && carac->canNotify()) {
            caracteristicaHID = carac;
            Serial.printf("[XBOX BLE] Input Report encontrado. Handle: 0x%04X\n", carac->getHandle());
            break;
        }
    }

    if (caracteristicaHID == nullptr) {
        Serial.println("[XBOX BLE] Característica HID com Notify não encontrada.");
        clienteBLE->disconnect();
        return false;
    }

    caracteristicaHID->registerForNotify(notificacaoHID);
    Serial.println("[XBOX BLE] Subscrito. Comando ativo!");
    return true;
}

// =============================================================================
// Inicialização Pública
// =============================================================================
void inicializarBLE() {
    BLEDevice::init("Rover-ESP32");
    BLEDevice::setMTU(512); // Fundamental para não cortar os pacotes longos da Microsoft
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new CallbackSeguranca());
    Serial.println("[XBOX BLE] Pilha BLE inicializada.");
}

// =============================================================================
// Task Principal (Roda no Core 0 em Background)
// =============================================================================
void TaskXbox(void* parametros) {
    estadoAtual = XBOX_CONECTANDO;

    for (;;) {
        switch (estadoAtual) {
            case XBOX_CONECTANDO:
                segurancaEstabelecida = false;
                if (conectarESubscrever()) {
                    estadoAtual = XBOX_CONECTADO;
                } else {
                    clienteBLE = nullptr;
                    caracteristicaHID = nullptr;
                    vTaskDelay(pdMS_TO_TICKS(INTERVALO_RECONEXAO_MS));
                }
                break;

            case XBOX_CONECTADO:
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case XBOX_DESCONECTADO:
                clienteBLE = nullptr;
                caracteristicaHID = nullptr;
                vTaskDelay(pdMS_TO_TICKS(INTERVALO_RECONEXAO_MS));
                estadoAtual = XBOX_CONECTANDO;
                break;
        }
    }
}