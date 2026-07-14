// =============================================================================
// driver_tcp.h - Wi-Fi, comandos TCP e telemetria BLE enquadrada
// =============================================================================

#ifndef DRIVER_TCP_H
#define DRIVER_TCP_H

#include <Arduino.h>
#include <WiFi.h>

constexpr uint16_t PORTA_TCP_COMANDOS = 8080;
constexpr uint16_t PORTA_TCP_TELEMETRIA = 8081;
constexpr size_t TAMANHO_MAX_PACOTE_TELEMETRIA = 512;

// Inicia a conexao Wi-Fi de forma assincrona. Nunca bloqueia o boot do BLE.
void inicializarWiFi(const char* ssid, const char* password);

// Cria a fila de telemetria e, quando houver Wi-Fi, inicia os dois servidores.
// Retorna false apenas se nao foi possivel reservar a fila.
bool iniciarServidorTCP();

// Deve ser chamada pelo loop Arduino. Mantem o Wi-Fi conectado, aceita os
// clientes e e o unico ponto que usa WiFiClient.
void verificarConexoesTCP();

// Segura para ser chamada pelo callback BLE: apenas copia o pacote para uma
// fila FreeRTOS. O frame enviado em 8081 e [uint16_t big-endian][payload].
bool enfileirarTelemetria(const uint8_t* dados, size_t tamanho);

#endif  // DRIVER_TCP_H
