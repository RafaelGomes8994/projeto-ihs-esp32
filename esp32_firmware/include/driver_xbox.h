// =============================================================================
// driver_xbox.h - Cliente BLE/HID para o controle Xbox
// =============================================================================

#ifndef DRIVER_XBOX_H
#define DRIVER_XBOX_H

#include <Arduino.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include "wifi_config.h"

// Pode ser sobrescrito em wifi_secrets.h ou por uma build flag sem editar o
// driver. O endereco deve ser o MAC BLE do controle Xbox pareado.
#ifndef MAC_XBOX
#define MAC_XBOX "14:cb:65:8c:9c:55"
#endif

#define UUID_SERVICO_HID        "00001812-0000-1000-8000-00805f9b34fb"
#define UUID_CARACTERISTICA_HID "00002a4d-0000-1000-8000-00805f9b34fb"

constexpr uint32_t INTERVALO_RECONEXAO_MS = 3000;
constexpr uint32_t INTERVALO_HEARTBEAT_BLE_MS = 100;

enum class EstadoBLE_t : uint8_t {
    CONECTANDO,
    CONECTADO,
    DESCONECTADO
};

void inicializarBLE();
void TaskXbox(void* parametros);

#endif  // DRIVER_XBOX_H
