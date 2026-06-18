import socket
import sys

PORTA_TCP = 8080

def iniciar_telemetria():
    print("==================================================")
    print("   ROVER FPV - TELEMETRIA E SNIFFING TCP/BLE      ")
    print("==================================================")
    
    # Pede o IP dinamicamente para não teres de editar o código na hora
    ip_esp32 = input("Digite o IP do Rover (ver no Hotspot): ").strip()
    
    print(f"\n[*] Tentando conectar a {ip_esp32}:{PORTA_TCP}...")
    
    try:
        # Cria a conexão TCP
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((ip_esp32, PORTA_TCP))
        print("[*] Conexão TCP estabelecida com sucesso!")
        print("[*] Aguardando pacotes HID do Xbox...\n")
        
        while True:
            # O pacote do Xbox que descobrimos tem 16 bytes
            dados = s.recv(16)
            
            if not dados:
                print("[!] Conexão perdida.")
                break
                
            if len(dados) < 14:
                continue # Ignora pacotes de status curtos

            # 1. Formata os bytes em Hexadecimal (O "Sniff" para impressionar o prof)
            hex_str = " ".join(f"{b:02X}" for b in dados)
            
            # 2. Aplica a tua Engenharia Reversa (Byte 13)
            estado_botoes = dados[13]
            acao = "PARADO (Sem comando)"
            
            # Máscaras bitwise que mapeaste
            if estado_botoes & 0x08:
                acao = "FRENTE (Botão X pressionado)"
            elif estado_botoes & 0x10:
                acao = "TRÁS (Botão Y pressionado)"
            elif estado_botoes & 0x02:
                acao = "PARAR (Botão B pressionado)"
            
            # Só imprime se alguém estiver a carregar num botão 
            # (para não inundar o ecrã com pacotes vazios)
            if estado_botoes != 0x00:
                print(f"RAW: [{hex_str}]")
                print(f"--> DECODED: {acao}\n")
                
    except KeyboardInterrupt:
        print("\n[*] Telemetria encerrada pelo utilizador.")
    except Exception as e:
        print(f"\n[!] Erro de rede: {e}")
        print("[DICA] Confirma se o IP está correto e se o robô está ligado ao Hotspot.")

if __name__ == "__main__":
    iniciar_telemetria()