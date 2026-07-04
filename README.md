# Rover FPV - Interface Hardware/Software (IHS) 🚀

Este projeto foi desenvolvido como trabalho de avaliação para a disciplina de **Interface Hardware/Software (IHS)** na UFS, lecionada pelo Prof. Bruno Prado.

## 📌 Arquitetura do Sistema
O objetivo deste projeto é demonstrar o controlo de um chassi robótico (Rover 2WD) baseado na ESP32, integrando conceitos de baixo nível, concorrência e programação no espaço do núcleo (Kernel Space).

O sistema possui uma arquitetura híbrida com **Dois Modos de Operação Simultâneos** (Múltiplos Produtores, Único Consumidor via FreeRTOS):

1. **Modo BLE Direto:** A ESP32 comunica diretamente com o comando Xbox Series S via Bluetooth Low Energy (BLE), interpretando pacotes HID e traduzindo para comandos de motores.
2. **Modo Rede (Linux Kernel Module):** Um módulo de núcleo (`rover_kdriver.ko`) escrito em C que corre num sistema Linux host. Este módulo escuta eventos do comando diretamente do subsistema de input (`/dev/input`) em Ring 0 e despacha comandos TCP para a rede. A ESP32 atua como Servidor TCP e converte as mensagens em ações.

## 📂 Estrutura do Repositório

* **`/esp32_firmware`**: Contém o projeto PlatformIO para a ESP32.
    * `src/main.cpp`: Ponto de entrada, configuração do FreeRTOS e inicialização da rede/BLE.
    * `src/driver_motores.cpp`: Consumidor da Fila. Controla os pinos da Ponte H L298N.
    * `src/driver_tcp.cpp`: Produtor 1. Escuta a porta 8080 e injeta na fila.
    * `src/driver_xbox.cpp`: Produtor 2. Trata a máquina de estados BLE e extrai o offset 13 (Botões X, Y, B).
    * `src/calculo_pwm.S`: Rotina Inline/External em Assembly Xtensa LX6 para conversão do duty-cycle do motor.
* **`/linux_kernel_module`**: Contém o código C para o módulo do kernel (LKM) e o `Makefile` de compilação.

## ⚙️ Tecnologias e Conceitos Aplicados
* **Microcontrolador:** ESP32 (Arquitetura Xtensa de 32 bits).
* **Tempo Real:** FreeRTOS (Criação de Tasks, Filas assíncronas para evitar race conditions, alocação de núcleos).
* **Baixo Nível:** Instruções Assembly para arquitetura do hospedeiro e cálculo matemático em L1.
* **Sistema Operativo:** Programação Kernel Space, `struct workqueue_struct`, e manipulação nativa de sockets (Kernel Sockets).




