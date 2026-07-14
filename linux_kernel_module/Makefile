# ==============================================================================
# Makefile — Drivezao (Driver Ring 0 do Rover FPV)
# Disciplina: IHS — Prof. Bruno Prado / UFS
# ==============================================================================

# --- Configuração do alvo ---
MODULO    := Drivezao
ESP_IP    := 192.168.100.65
ESP_PORT  := 8080
HEARTBEAT_MS := 100
MAC_XBOX  := 14:CB:65:8C:9C:55

# --- Paths do kernel build ---
KDIR      := /lib/modules/$(shell uname -r)/build
PWD_DIR   := $(shell pwd)

# --- Cores ANSI para o terminal ---
RED    := \033[31m
GREEN  := \033[32m
YELLOW := \033[33m
BLUE   := \033[34m
CYAN   := \033[36m
BOLD   := \033[1m
RESET  := \033[0m

# Objeto do módulo
obj-m += $(MODULO).o

# ==============================================================================
# TARGETS PRINCIPAIS
# ==============================================================================

.PHONY: all clean load unload reload status log watch dmesg-full check bt-pair bt-status help

# --- Compilação ---
all: _check_headers
	@printf "$(BOLD)$(BLUE)[DRIVEZAO] Compilando modulo...$(RESET)\n"
	@make -C $(KDIR) M=$(PWD_DIR) modules
	@printf "$(BOLD)$(GREEN)[DRIVEZAO] OK — $(MODULO).ko gerado$(RESET)\n"

# --- Limpeza ---
clean:
	@printf "$(YELLOW)[DRIVEZAO] Limpando artefatos de build...$(RESET)\n"
	@make -C $(KDIR) M=$(PWD_DIR) clean
	@printf "$(GREEN)[DRIVEZAO] Limpo.$(RESET)\n"

# --- Carrega o módulo no kernel ---
load: all
	@printf "$(BOLD)$(CYAN)\n"
	@printf "╔══════════════════════════════════════╗\n"
	@printf "║  Carregando Drivezao no kernel...    ║\n"
	@printf "╠══════════════════════════════════════╣\n"
	@printf "║  ESP32 : $(ESP_IP):$(ESP_PORT)           ║\n"
	@printf "║  Heartbeat: $(HEARTBEAT_MS) ms                    ║\n"
	@printf "╚══════════════════════════════════════╝\n"
	@printf "$(RESET)\n"
	@sudo insmod $(MODULO).ko esp_ip="$(ESP_IP)" esp_port=$(ESP_PORT) heartbeat_ms=$(HEARTBEAT_MS)
	@if lsmod | grep -q "^$(MODULO)"; then \
		printf "$(BOLD)$(GREEN)[DRIVEZAO] Modulo ativo no kernel!$(RESET)\n"; \
	else \
		printf "$(RED)[DRIVEZAO] ERRO — modulo nao carregou. Veja: sudo dmesg | tail -20$(RESET)\n"; \
		exit 1; \
	fi

# --- Remove o módulo do kernel ---
unload:
	@if lsmod | grep -q "^$(MODULO)"; then \
		printf "$(YELLOW)[DRIVEZAO] Removendo modulo...$(RESET)\n"; \
		sudo rmmod $(MODULO); \
		printf "$(GREEN)[DRIVEZAO] Modulo removido.$(RESET)\n"; \
	else \
		printf "$(YELLOW)[DRIVEZAO] Modulo nao estava carregado.$(RESET)\n"; \
	fi

# --- Ciclo completo: remove → limpa → recompila → carrega ---
reload: unload clean all load
	@printf "$(BOLD)$(GREEN)[DRIVEZAO] Reload completo. Pronto.$(RESET)\n"

# ==============================================================================
# MONITORAMENTO
# ==============================================================================

# --- Status completo do módulo ---
status:
	@printf "$(BOLD)$(CYAN)\n"
	@printf "╔══════════════════════════════════════════════╗\n"
	@printf "║          STATUS DO DRIVEZAO                  ║\n"
	@printf "╚══════════════════════════════════════════════╝\n"
	@printf "$(RESET)\n"
	@printf "$(BOLD)--- Modulo no kernel (lsmod) ---$(RESET)\n"
	@lsmod | grep "$(MODULO)" || printf "  $(YELLOW)Modulo nao carregado$(RESET)\n"
	@printf "\n$(BOLD)--- Dispositivos de entrada detectados ---$(RESET)\n"
	@cat /proc/bus/input/devices 2>/dev/null | grep -A4 -i "Xbox\|xbox\|drivezao" \
		|| printf "  $(YELLOW)Nenhum dispositivo Xbox encontrado$(RESET)\n"
	@printf "\n$(BOLD)--- Ultimas mensagens do kernel (DRIVEZAO) ---$(RESET)\n"
	@sudo dmesg | grep -i "DRIVEZAO" | tail -30 \
		|| printf "  $(YELLOW)Nenhuma mensagem ainda$(RESET)\n"
	@printf "\n$(BOLD)--- Conexao TCP com a ESP32 ---$(RESET)\n"
	@ss -tn 2>/dev/null | grep "$(ESP_IP):$(ESP_PORT)" \
		|| printf "  $(YELLOW)Nenhuma conexao ativa para $(ESP_IP):$(ESP_PORT)$(RESET)\n"

# --- Log em tempo real filtrado: APENAS mensagens do Drivezao ---
# Equivalente a tail -f do kernel. Ctrl+C para sair.
log:
	@printf "$(BOLD)$(CYAN)[DRIVEZAO] Log em tempo real (Ctrl+C para sair)...$(RESET)\n"
	@sudo dmesg -w | grep --line-buffered "DRIVEZAO"

# Alias para log
watch: log

# --- Log completo do kernel sem filtro (para depurar erros de boot do módulo) ---
dmesg-full:
	@printf "$(BOLD)$(CYAN)[DRIVEZAO] dmesg completo em tempo real (Ctrl+C para sair)...$(RESET)\n"
	@sudo dmesg -wH

# ==============================================================================
# AMBIENTE E BLUETOOTH
# ==============================================================================

# --- Verifica pré-requisitos do ambiente ---
check:
	@printf "$(BOLD)$(BLUE)\n[DRIVEZAO] Verificando ambiente de compilacao...\n$(RESET)\n"
	@if [ -d "$(KDIR)" ]; then \
		printf "$(GREEN)  [OK]    Kernel headers: $(KDIR)$(RESET)\n"; \
	else \
		printf "$(RED)  [FALTA] Kernel headers ausentes$(RESET)\n"; \
		printf "          Execute: sudo apt install linux-headers-$(shell uname -r)\n"; \
	fi
	@if command -v bluetoothctl >/dev/null 2>&1; then \
		printf "$(GREEN)  [OK]    bluetoothctl disponivel$(RESET)\n"; \
	else \
		printf "$(YELLOW)  [AVISO] bluetoothctl ausente$(RESET)\n"; \
		printf "          Execute: sudo apt install bluez\n"; \
	fi
	@if command -v gcc >/dev/null 2>&1; then \
		printf "$(GREEN)  [OK]    gcc: $$(gcc --version | head -1)$(RESET)\n"; \
	else \
		printf "$(RED)  [FALTA] gcc nao encontrado$(RESET)\n"; \
	fi
	@if lsmod | grep -q "^$(MODULO)"; then \
		printf "$(GREEN)  [OK]    Modulo $(MODULO) carregado$(RESET)\n"; \
	else \
		printf "$(YELLOW)  [INFO]  Modulo $(MODULO) nao carregado$(RESET)\n"; \
	fi
	@printf "\n$(BOLD)  Kernel em uso: $(shell uname -r)$(RESET)\n"
	@printf "  ESP32 alvo  : $(ESP_IP):$(ESP_PORT)\n"
	@printf "  MAC Xbox    : $(MAC_XBOX)\n\n"

# --- Pareamento do Xbox Series S via Bluetooth ---
bt-pair:
	@printf "$(BOLD)$(CYAN)\n"
	@printf "╔══════════════════════════════════════════════════════╗\n"
	@printf "║  Pareamento Xbox Series S via Bluetooth              ║\n"
	@printf "╠══════════════════════════════════════════════════════╣\n"
	@printf "║  1. Segure o botao Sync do Xbox por ~3 segundos     ║\n"
	@printf "║     (LED pisca rapido = modo pareamento ativo)      ║\n"
	@printf "║  2. Execute os comandos abaixo no bluetoothctl      ║\n"
	@printf "╚══════════════════════════════════════════════════════╝\n"
	@printf "$(RESET)\n"
	@printf "$(YELLOW)Comandos a executar no bluetoothctl:$(RESET)\n"
	@printf "  power on\n"
	@printf "  agent on\n"
	@printf "  default-agent\n"
	@printf "  scan on\n"
	@printf "  pair $(MAC_XBOX)\n"
	@printf "  connect $(MAC_XBOX)\n"
	@printf "  trust $(MAC_XBOX)\n"
	@printf "  scan off\n"
	@printf "  exit\n\n"
	@printf "$(BOLD)Abrindo bluetoothctl...$(RESET)\n"
	@bluetoothctl

# --- Verifica se o Xbox está conectado via BT ---
bt-status:
	@printf "$(BOLD)$(BLUE)[BT] Status do controle Xbox:$(RESET)\n"
	@bluetoothctl info $(MAC_XBOX) 2>/dev/null || \
		printf "$(YELLOW)  Xbox nao pareado ou bluetoothctl indisponivel$(RESET)\n"
	@printf "\n$(BOLD)Dispositivos de entrada:$(RESET)\n"
	@ls /dev/input/ 2>/dev/null
	@printf "\n$(BOLD)Processos hid/xbox no kernel:$(RESET)\n"
	@cat /proc/bus/input/devices 2>/dev/null | grep -B1 -A6 -i "xbox\|wireless controller" \
		|| printf "  $(YELLOW)Nenhum dispositivo Xbox detectado$(RESET)\n"

# ==============================================================================
# TARGETS INTERNOS
# ==============================================================================

_check_headers:
	@if [ ! -d "$(KDIR)" ]; then \
		printf "$(RED)[ERRO] Kernel headers nao encontrados em $(KDIR)$(RESET)\n"; \
		printf "       Execute: sudo apt install linux-headers-$(shell uname -r)\n"; \
		exit 1; \
	fi

# ==============================================================================
# AJUDA
# ==============================================================================

help:
	@printf "$(BOLD)$(CYAN)\n"
	@printf "╔══════════════════════════════════════════════════════════╗\n"
	@printf "║        DRIVEZAO — Ring 0 Driver para Rover FPV          ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  COMPILACAO E DEPLOY                                     ║\n"
	@printf "║    make all          Compila o modulo                    ║\n"
	@printf "║    make load         Compila e carrega no kernel         ║\n"
	@printf "║    make unload       Remove o modulo do kernel           ║\n"
	@printf "║    make reload       Remove + limpa + compila + carrega  ║\n"
	@printf "║    make clean        Remove artefatos de build           ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  MONITORAMENTO                                           ║\n"
	@printf "║    make status       Status completo + dmesg filtrado    ║\n"
	@printf "║    make log          dmesg -w filtrado em tempo real     ║\n"
	@printf "║    make watch        Alias para make log                 ║\n"
	@printf "║    make dmesg-full   dmesg -wH completo sem filtro       ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  BLUETOOTH                                               ║\n"
	@printf "║    make bt-pair      Guia + abre bluetoothctl            ║\n"
	@printf "║    make bt-status    Status do Xbox no kernel            ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  AMBIENTE                                                ║\n"
	@printf "║    make check        Verifica prerequisitos              ║\n"
	@printf "║    make help         Esta mensagem                       ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  PARAMETROS SOBREPONIVEIS                                ║\n"
	@printf "║    make load ESP_IP=192.168.1.50                         ║\n"
	@printf "║    make load ESP_PORT=8080 HEARTBEAT_MS=100              ║\n"
	@printf "╠══════════════════════════════════════════════════════════╣\n"
	@printf "║  Alvo atual: $(ESP_IP):$(ESP_PORT)                       ║\n"
	@printf "║  Kernel:     $(shell uname -r)                 ║\n"
	@printf "╚══════════════════════════════════════════════════════════╝\n"
	@printf "$(RESET)\n"
