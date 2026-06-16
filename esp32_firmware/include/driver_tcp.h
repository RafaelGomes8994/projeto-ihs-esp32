#ifndef DRIVER_TCP_H
#define DRIVER_TCP_H

#include <WiFi.h>

// Protótipos das funções
void inicializarWiFi(const char* ssid, const char* password);
void iniciarServidorTCP();
void verificarConexoesTCP();

#endif