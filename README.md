# Rover IHS com ESP32

Projeto da disciplina de Interface Hardware/Software para controlar um rover 2WD
com ESP32-CAM, ponte H L9110S, controle Xbox via BLE, controle alternativo via
TCP a partir de um modulo de kernel Linux e telemetria HID pela rede.

O sistema foi desenvolvido em tres partes:

- firmware da ESP32, responsavel por BLE, Wi-Fi, TCP, telemetria e motores;
- `Drivezao`, modulo de kernel Linux que le eventos de entrada e envia comandos
  TCP para a ESP32;
- scripts Python para capturar, visualizar e analisar os reports HID do controle.

## Estrutura do Repositorio

```text
esp32_firmware/          Firmware da ESP32-CAM
linux_kernel_module/     Modulo de kernel Linux Drivezao
analise_Bytes_Controle/  Scripts Python de telemetria e analise HID
```

Arquivos principais:

| Arquivo | Funcao |
| --- | --- |
| `esp32_firmware/src/main.cpp` | Inicializa fila, motores, Wi-Fi/TCP e BLE. |
| `esp32_firmware/src/driver_motores.cpp` | Controla a ponte H L9110S, PWM, watchdog e arbitragem. |
| `esp32_firmware/src/driver_tcp.cpp` | Mantem Wi-Fi, servidor de comandos e servidor de telemetria. |
| `esp32_firmware/src/driver_xbox.cpp` | Conecta no controle Xbox por BLE/HID e interpreta botoes. |
| `esp32_firmware/src/calculo_pwm.S` | Rotina Assembly Xtensa para calcular o duty cycle do PWM. |
| `esp32_firmware/include/wifi_config.h` | Carrega credenciais locais sem expor senha no repositorio. |
| `esp32_firmware/include/wifi_secrets.example.h` | Modelo para criar o arquivo local de Wi-Fi. |
| `linux_kernel_module/Drivezao.c` | Driver Linux que envia comandos TCP para o rover. |
| `linux_kernel_module/Makefile` | Compila, carrega, remove e monitora o modulo Linux. |
| `analise_Bytes_Controle/protocolo_telemetria.py` | Implementa o leitor do protocolo de telemetria TCP. |
| `analise_Bytes_Controle/demonstracaoTCP.py` | Mostra os reports HID recebidos em tempo real. |
| `analise_Bytes_Controle/xbox_sniff.py` | Grava payloads HID recebidos pela telemetria. |
| `analise_Bytes_Controle/analisar_dump.py` | Analisa diferencas entre dumps HID salvos. |

## Arquitetura

O rover possui dois caminhos de controle que chegam ao mesmo consumidor de
comandos no firmware:

```text
Controle Xbox BLE ------\
                         \
                          -> fila FreeRTOS -> TaskMotores -> L9110S -> motores
Drivezao Linux -> TCP ---/

Controle Xbox BLE -> ESP32 -> TCP 8081 -> scripts Python
```

A ESP32 recebe comandos de duas fontes:

- BLE direto do controle Xbox;
- TCP vindo do `Drivezao`, rodando em um host Linux.

Mesmo com duas fontes, os motores sao controlados por uma unica task. Isso evita
que BLE e TCP escrevam diretamente nos GPIOs ao mesmo tempo.

## Como Foi Feito

### Firmware ESP32

O firmware usa Arduino para ESP32 e FreeRTOS. A inicializacao principal cria uma
fila de comandos com uma posicao, configura os motores, inicia a rede, cria os
servidores TCP e inicia a task responsavel pelo controle Xbox BLE.

A fila de comandos guarda sempre o estado mais recente. Em vez de acumular uma
lista de movimentos, o firmware trabalha com estado: frente, tras ou parar.
Isso evita que comandos antigos sejam executados atrasados.

### Controle dos Motores

A ponte H usada e uma L9110S. A pinagem atual no firmware e:

| Sinal | GPIO |
| --- | --- |
| Motor 1 frente | `13` |
| Motor 1 tras | `14` |
| Motor 2 frente | `4` |
| Motor 2 tras | `15` |

O PWM usa quatro canais LEDC, frequencia de `20 kHz` e resolucao de `10 bits`.
A rotina `calcularPWM`, escrita em Assembly Xtensa, recebe uma entrada de
`0..255` e converte para `0..1020`, faixa usada pelo LEDC.

O controle dos motores inclui:

- inicializacao sempre em repouso;
- parada fisica antes de mudar de direcao;
- tempo morto de `500 us` durante reversao;
- watchdog de `500 ms` para parar se o heartbeat sumir;
- prioridade para qualquer comando de parada;
- arbitragem entre comandos vindos de BLE e TCP.

### Controle Xbox por BLE

O firmware se conecta ao controle Xbox usando o servico HID BLE. Ele procura a
caracteristica de input report com notificacao, registra callback e interpreta
os bytes recebidos.

Mapeamento usado no controle:

| Botao | Acao |
| --- | --- |
| `X` | Frente |
| `Y` | Tras |
| `B` | Parar |

Os reports HID tambem sao enviados para uma fila de telemetria. O callback BLE
nao acessa diretamente o TCP; ele apenas copia os dados para fila, evitando
bloqueio dentro da pilha Bluetooth.

### TCP

A ESP32 usa duas portas TCP:

| Porta | Uso |
| --- | --- |
| `8080` | Comandos de movimento |
| `8081` | Telemetria HID |

Os comandos aceitos na porta `8080` sao:

| Byte | Acao |
| --- | --- |
| `F` | Frente |
| `T` | Tras |
| `P` | Parar |

A telemetria usa frames com cabecalho de tamanho:

```text
[uint16 big-endian tamanho][payload HID bruto]
```

Esse formato foi escolhido porque TCP e um fluxo de bytes. O cabecalho permite
que o cliente saiba exatamente quantos bytes pertencem a cada report HID.

### Drivezao

O `Drivezao` e um modulo de kernel Linux escrito em C. Ele registra um
`input_handler`, detecta dispositivos compativeis com controle Xbox ou teclado e
envia comandos para a ESP32 por socket TCP em kernel space.

Mapeamento no `Drivezao`:

| Entrada | Acao |
| --- | --- |
| Xbox `X` ou seta para cima | Frente |
| Xbox `Y` ou seta para baixo | Tras |
| Xbox `A`, Xbox `B` ou espaco | Parar |

O modulo usa uma workqueue para fazer a comunicacao TCP fora do callback de
input. Assim, o callback apenas atualiza estado e agenda trabalho; a rede fica
em um contexto apropriado.

Enquanto um comando de movimento esta pressionado, o modulo envia heartbeats
periodicos. Quando a tecla ou botao e solto, ele envia parada imediatamente.

## Configuracao de Wi-Fi e Controle

As credenciais reais ficam fora do Git. Para configurar, crie o arquivo local a
partir do exemplo:

```bash
cp esp32_firmware/include/wifi_secrets.example.h esp32_firmware/include/wifi_secrets.h
```

Depois preencha:

```c
#define ROVER_WIFI_SSID "nome-da-rede"
#define ROVER_WIFI_PASSWORD "senha-da-rede"
```

O MAC BLE do controle tambem pode ser definido no mesmo arquivo:

```c
#define MAC_XBOX "aa:bb:cc:dd:ee:ff"
```

## Compilacao Pelo Makefile

A compilacao documentada neste projeto e feita pelo Makefile do modulo Linux,
localizado em `linux_kernel_module/Makefile`.

O modulo deve ser compilado em um ambiente Linux com headers do kernel
instalados. Pacotes comuns:

```bash
sudo apt install build-essential linux-headers-$(uname -r) bluez
```

Verificar o ambiente:

```bash
make -C linux_kernel_module check
```

Compilar o modulo:

```bash
make -C linux_kernel_module all
```

Carregar o modulo apontando para a ESP32:

```bash
make -C linux_kernel_module load ESP_IP=<IP_DA_ESP32> ESP_PORT=8080 HEARTBEAT_MS=100
```

Ver status:

```bash
make -C linux_kernel_module status
```

Acompanhar logs:

```bash
make -C linux_kernel_module log
```

Remover o modulo:

```bash
make -C linux_kernel_module unload
```

Limpar artefatos de build:

```bash
make -C linux_kernel_module clean
```

Alvos principais do Makefile:

| Alvo | Funcao |
| --- | --- |
| `check` | Verifica headers, compilador, Bluetooth e estado do modulo. |
| `all` | Compila `Drivezao.ko`. |
| `load` | Compila e carrega o modulo no kernel. |
| `unload` | Remove o modulo do kernel. |
| `reload` | Remove, limpa, recompila e carrega novamente. |
| `status` | Mostra modulo, entradas detectadas, logs e conexao TCP. |
| `log` | Mostra logs filtrados do Drivezao em tempo real. |
| `watch` | Alias para `log`. |
| `dmesg-full` | Mostra `dmesg` completo em tempo real. |
| `bt-pair` | Abre fluxo de pareamento Bluetooth via `bluetoothctl`. |
| `bt-status` | Mostra status do controle Xbox no Linux. |
| `clean` | Remove artefatos de compilacao. |
| `help` | Mostra ajuda do Makefile. |

Parametros aceitos na carga:

| Parametro | Padrao | Descricao |
| --- | --- | --- |
| `ESP_IP` | `192.168.100.65` | IP da ESP32. |
| `ESP_PORT` | `8080` | Porta TCP de comandos. |
| `HEARTBEAT_MS` | `100` | Intervalo entre comandos mantidos. |

## Telemetria e Analise HID

Com a ESP32 conectada na rede, os scripts Python podem visualizar ou gravar a
telemetria da porta `8081`.

Visualizar reports HID:

```bash
python analise_Bytes_Controle/demonstracaoTCP.py <IP_DA_ESP32>
```

Mostrar tambem reports sem comando mapeado:

```bash
python analise_Bytes_Controle/demonstracaoTCP.py <IP_DA_ESP32> --todos
```

Capturar payloads para arquivo:

```bash
python analise_Bytes_Controle/xbox_sniff.py <IP_DA_ESP32> --arquivo analise_Bytes_Controle/captura_xbox.txt
```

Analisar um dump salvo:

```bash
python analise_Bytes_Controle/analisar_dump.py analise_Bytes_Controle/xbox_dump.txt
python analise_Bytes_Controle/analisar_dump.py analise_Bytes_Controle/xbox_dump.txt --detalhado
```

## Sequencia Recomendada de Teste

1. Configurar `wifi_secrets.h` com SSID, senha e, se necessario, MAC do controle.
2. Gravar o firmware na ESP32-CAM pelo ambiente de desenvolvimento usado no projeto.
3. Abrir o monitor serial e anotar o IP mostrado pela ESP32.
4. Testar a telemetria com `demonstracaoTCP.py`.
5. No Linux, rodar `make -C linux_kernel_module check`.
6. Compilar o modulo com `make -C linux_kernel_module all`.
7. Carregar com `make -C linux_kernel_module load ESP_IP=<IP_DA_ESP32>`.
8. Acompanhar `make -C linux_kernel_module log`.
9. Testar primeiro com as rodas suspensas.
10. Testar no chao somente depois que parada e direcoes estiverem confirmadas.

## Seguranca de Bancada

- Teste sempre com as rodas fora do chao primeiro.
- Tenha um meio fisico de cortar a energia dos motores.
- Nao alimente motores pelo 3,3 V da ESP32.
- Use GND comum entre ESP32 e ponte H.
- Confira a polaridade da ponte H antes de colocar o rover no chao.
- Envie `P` ou solte os botoes antes de mexer em cabos, bateria ou fonte.
