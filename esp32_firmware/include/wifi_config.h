// =============================================================================
// wifi_config.h - Configuracao local de rede sem credenciais no Git
// =============================================================================

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// Copie wifi_secrets.example.h para wifi_secrets.h e preencha os valores. O
// arquivo real e ignorado pelo Git. Sem ele o firmware ainda compila e o modo
// BLE continua disponivel, mas os servidores TCP permanecem desligados.
#if defined(__has_include)
#  if __has_include("wifi_secrets.h")
#    include "wifi_secrets.h"
#  endif
#endif

#ifndef ROVER_WIFI_SSID
#define ROVER_WIFI_SSID ""
#endif

#ifndef ROVER_WIFI_PASSWORD
#define ROVER_WIFI_PASSWORD ""
#endif

#endif  // WIFI_CONFIG_H
