import socket

# Configuração
ESP32_IP = '192.168.100.34' # O IP que aparece no teu Serial Monitor
ESP32_PORT = 8080

def capturar():
    print(f"[*] Conectando ao Rover: {ESP32_IP}:{ESP32_PORT}")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((ESP32_IP, ESP32_PORT))
        
        with open("xbox_dump.txt", "a") as f:
            print("[*] Gravando em xbox_dump.txt... (Ctrl+C para parar)")
            while True:
                data = s.recv(16) # O tamanho do pacote que observamos antes
                if not data: break
                
                # Formata para hex e grava no arquivo
                hex_data = data.hex(' ')
                f.write(f"{hex_data}\n")
                f.flush() # Garante que escreve no disco imediatamente
                print(f"Capturado: {hex_data}")
                
    except Exception as e:
        print(f"[!] Erro: {e}")

if __name__ == "__main__":
    capturar()