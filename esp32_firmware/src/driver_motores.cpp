// =============================================================================
// driver_motores.cpp - Ponte H L9110S com PWM LEDC e fail-safe de movimento
// =============================================================================

#include "driver_motores.h"

namespace {

// Pinagem usada pela montagem atual do ESP32-CAM e da L9110S.
constexpr uint8_t PINO_MOTOR1_F = 13;
constexpr uint8_t PINO_MOTOR1_B = 14;
constexpr uint8_t PINO_MOTOR2_F = 4;
constexpr uint8_t PINO_MOTOR2_B = 15;

// APIs LEDC abaixo sao as da Arduino-ESP32 2.x. Quatro canais permitem que
// cada entrada IA/IB da L9110S receba PWM real, sem usar digitalWrite para duty.
constexpr uint8_t CANAL_MOTOR1_F = 0;
constexpr uint8_t CANAL_MOTOR1_B = 1;
constexpr uint8_t CANAL_MOTOR2_F = 2;
constexpr uint8_t CANAL_MOTOR2_B = 3;
constexpr uint32_t FREQUENCIA_PWM_HZ = 20000;
constexpr uint8_t RESOLUCAO_PWM_BITS = 10;
constexpr uint8_t ENTRADA_PWM_PADRAO = 255;  // ~100% de potencia (1020/1023)
constexpr uint32_t TEMPO_MORTO_REVERSAO_US = 500;
constexpr TickType_t INTERVALO_VERIFICACAO_TICKS = pdMS_TO_TICKS(20);

// Declaracao da rotina em Assembly Xtensa: 0..255 -> 0..1020.
extern "C" uint32_t calcularPWM(uint8_t entrada);

uint32_t pwmVelocidade = 0;
Direcao direcaoFisica = Direcao::PARAR;

// xQueueOverwrite preserva apenas o estado mais novo. Esta flag separada faz
// com que PARAR nao seja apagado por um F/T que chegue logo depois da parada.
portMUX_TYPE muxParadaPendente = portMUX_INITIALIZER_UNLOCKED;
volatile bool paradaPendente = false;

const char* nomeFonte(FonteComando fonte) {
    switch (fonte) {
        case FonteComando::TCP: return "TCP";
        case FonteComando::BLE: return "BLE";
        case FonteComando::SISTEMA:
        default: return "SISTEMA";
    }
}

const char* nomeDirecao(Direcao direcao) {
    switch (direcao) {
        case Direcao::FRENTE: return "FRENTE";
        case Direcao::TRAS: return "TRAS";
        case Direcao::PARAR:
        default: return "PARAR";
    }
}

void escreverTodosPWM(uint32_t duty) {
    ledcWrite(CANAL_MOTOR1_F, duty);
    ledcWrite(CANAL_MOTOR1_B, duty);
    ledcWrite(CANAL_MOTOR2_F, duty);
    ledcWrite(CANAL_MOTOR2_B, duty);
}

void pararFisicamente() {
    escreverTodosPWM(0);
    direcaoFisica = Direcao::PARAR;
}

bool passouDoTempo(uint32_t agora, uint32_t referencia, uint32_t intervalo) {
    return static_cast<uint32_t>(agora - referencia) > intervalo;
}

bool consumirParadaPendente() {
    bool haviaParada = false;
    portENTER_CRITICAL(&muxParadaPendente);
    haviaParada = paradaPendente;
    paradaPendente = false;
    portEXIT_CRITICAL(&muxParadaPendente);
    return haviaParada;
}

void aplicarParadaSegura(Direcao& direcaoAtiva,
                         FonteComando& fonteAtiva,
                         uint32_t& ultimoComandoMs,
                         const char* motivo) {
    const bool estavaEmMovimento = direcaoAtiva != Direcao::PARAR;
    pararFisicamente();
    direcaoAtiva = Direcao::PARAR;
    fonteAtiva = FonteComando::SISTEMA;
    ultimoComandoMs = 0;

    if (estavaEmMovimento) {
        Serial.printf("[MOTORES] PARAR (%s).\n", motivo);
    }
}

void tratarComando(const ComandoMotor& comando,
                   Direcao& direcaoAtiva,
                   FonteComando& fonteAtiva,
                   uint32_t& ultimoComandoMs) {
    const uint32_t agora = millis();

    // PARAR sempre vence, mesmo quando a outra fonte possui a concessao ativa.
    if (comando.direcao == Direcao::PARAR) {
        aplicarParadaSegura(direcaoAtiva, fonteAtiva, ultimoComandoMs,
                            nomeFonte(comando.fonte));
        return;
    }

    // Um comando que ficou parado por tempo demais na fila nao pode religar o
    // rover depois de um atraso excepcional de task.
    if (passouDoTempo(agora, comando.recebidoEmMs, TEMPO_LIMITE_COMANDO_MS)) {
        return;
    }

    // Arbitragem explicita: a primeira fonte que manda F/T ganha uma concessao
    // de 500 ms. Apenas ela pode renovar enquanto estiver viva; a outra fonte
    // so assume apos o timeout ou apos qualquer PARAR.
    const bool concessaoValida =
        fonteAtiva != FonteComando::SISTEMA &&
        direcaoAtiva != Direcao::PARAR &&
        !passouDoTempo(agora, ultimoComandoMs, TEMPO_LIMITE_COMANDO_MS);

    if (concessaoValida && comando.fonte != fonteAtiva) {
        return;
    }

    const bool mudouEstado =
        comando.direcao != direcaoAtiva || comando.fonte != fonteAtiva;

    fonteAtiva = comando.fonte;
    direcaoAtiva = comando.direcao;
    ultimoComandoMs = agora;
    processarComandoMotor(comando.direcao);

    if (mudouEstado) {
        Serial.printf("[MOTORES] %s assumiu: %s.\n",
                      nomeFonte(comando.fonte), nomeDirecao(comando.direcao));
    }
}

}  // namespace

bool enviarComandoMotor(Direcao direcao, FonteComando fonte) {
    if (filaComandos == nullptr) {
        return false;
    }

    ComandoMotor comando = {direcao, fonte, millis()};

    if (direcao == Direcao::PARAR) {
        portENTER_CRITICAL(&muxParadaPendente);
        paradaPendente = true;
        portEXIT_CRITICAL(&muxParadaPendente);
    }

    // A fila tem tamanho 1 por contrato. Assim nunca executamos movimentos
    // antigos em sequencia quando chegam comandos mais novos.
    return xQueueOverwrite(filaComandos, &comando) == pdPASS;
}

void inicializarMotores() {
    pinMode(PINO_MOTOR1_F, OUTPUT);
    pinMode(PINO_MOTOR1_B, OUTPUT);
    pinMode(PINO_MOTOR2_F, OUTPUT);
    pinMode(PINO_MOTOR2_B, OUTPUT);

    const bool canaisOk =
        ledcSetup(CANAL_MOTOR1_F, FREQUENCIA_PWM_HZ, RESOLUCAO_PWM_BITS) != 0 &&
        ledcSetup(CANAL_MOTOR1_B, FREQUENCIA_PWM_HZ, RESOLUCAO_PWM_BITS) != 0 &&
        ledcSetup(CANAL_MOTOR2_F, FREQUENCIA_PWM_HZ, RESOLUCAO_PWM_BITS) != 0 &&
        ledcSetup(CANAL_MOTOR2_B, FREQUENCIA_PWM_HZ, RESOLUCAO_PWM_BITS) != 0;

    ledcAttachPin(PINO_MOTOR1_F, CANAL_MOTOR1_F);
    ledcAttachPin(PINO_MOTOR1_B, CANAL_MOTOR1_B);
    ledcAttachPin(PINO_MOTOR2_F, CANAL_MOTOR2_F);
    ledcAttachPin(PINO_MOTOR2_B, CANAL_MOTOR2_B);

    pwmVelocidade = calcularPWM(ENTRADA_PWM_PADRAO);
    pararFisicamente();

    if (!canaisOk) {
        Serial.println("[MOTORES] ERRO: falha ao configurar canais LEDC.");
    }
    Serial.printf("[MOTORES] L9110S pronta: PWM real %lu Hz, duty %lu/1023.\n",
                  static_cast<unsigned long>(FREQUENCIA_PWM_HZ),
                  static_cast<unsigned long>(pwmVelocidade));
}

void processarComandoMotor(Direcao cmd) {
    if (cmd == direcaoFisica) {
        return;
    }

    const bool invertendo =
        direcaoFisica != Direcao::PARAR && cmd != Direcao::PARAR;

    // Nunca deixe as duas entradas de um canal ativas durante a reversao.
    escreverTodosPWM(0);
    if (invertendo) {
        delayMicroseconds(TEMPO_MORTO_REVERSAO_US);
    }

    switch (cmd) {
        case Direcao::FRENTE:
            ledcWrite(CANAL_MOTOR1_F, pwmVelocidade);
            ledcWrite(CANAL_MOTOR2_F, pwmVelocidade);
            break;

        case Direcao::TRAS:
            ledcWrite(CANAL_MOTOR1_B, pwmVelocidade);
            ledcWrite(CANAL_MOTOR2_B, pwmVelocidade);
            break;

        case Direcao::PARAR:
        default:
            break;
    }

    direcaoFisica = cmd;
}

void TaskMotores(void* parametros) {
    (void)parametros;

    Direcao direcaoAtiva = Direcao::PARAR;
    FonteComando fonteAtiva = FonteComando::SISTEMA;
    uint32_t ultimoComandoMs = 0;

    Serial.println("[MOTORES] TaskMotores iniciada no Core 1.");

    for (;;) {
        if (consumirParadaPendente()) {
            aplicarParadaSegura(direcaoAtiva, fonteAtiva, ultimoComandoMs,
                                "parada pendente");
        }

        ComandoMotor comando{};
        if (xQueueReceive(filaComandos, &comando, INTERVALO_VERIFICACAO_TICKS) == pdPASS) {
            tratarComando(comando, direcaoAtiva, fonteAtiva, ultimoComandoMs);
        }

        // Captura uma parada recebida enquanto a task aguardava a fila.
        if (consumirParadaPendente()) {
            aplicarParadaSegura(direcaoAtiva, fonteAtiva, ultimoComandoMs,
                                "parada pendente");
        }

        const uint32_t agora = millis();
        if (direcaoAtiva != Direcao::PARAR &&
            passouDoTempo(agora, ultimoComandoMs, TEMPO_LIMITE_COMANDO_MS)) {
            aplicarParadaSegura(direcaoAtiva, fonteAtiva, ultimoComandoMs,
                                "watchdog sem heartbeat");
        }
    }
}
