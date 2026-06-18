#ifndef DRIVER_TCP_H
#define DRIVER_TCP_H

#include <WiFi.h>

// Protótipos das funções
void inicializarWiFi(const char* ssid, const char* password);
void iniciarServidorTCP();
void verificarConexoesTCP();
void encaminharParaTCP(uint8_t* dados, size_t tamanho);

#endif