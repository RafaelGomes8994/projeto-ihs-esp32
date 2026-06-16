// =============================================================================
// driver_xbox.h — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#ifndef DRIVER_XBOX_H
#define DRIVER_XBOX_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>

// =============================================================================
// Configurações Globais do Bluetooth
// =============================================================================
// Substitui o MAC abaixo pelo endereço MAC real do teu comando Xbox
#define MAC_XBOX "14:cb:65:8c:9c:55"

// UUIDs padrão do Perfil HID (Human Interface Device)
#define UUID_SERVICO_HID        "00001812-0000-1000-8000-00805f9b34fb"
#define UUID_CARACTERISTICA_HID "00002a4d-0000-1000-8000-00805f9b34fb"

#define INTERVALO_RECONEXAO_MS  3000
#define MODO_SNIFFING           0 // Altera para 1 se quiseres ver os bytes brutos no terminal

// =============================================================================
// Estados da máquina de estados BLE
// =============================================================================
typedef enum {
    XBOX_CONECTANDO,    // A tentar estabelecer conexão BLE
    XBOX_CONECTADO,     // Subscrito às notificações e pronto
    XBOX_DESCONECTADO   // Ligação perdida, a aguardar reconexão
} EstadoBLE_t;

// =============================================================================
// Protótipos Públicos
// =============================================================================
void inicializarBLE();
void TaskXbox(void* parametros);

#endif // DRIVER_XBOX_H