def encontrar_mascara(arquivo):
    with open(arquivo, 'r') as f:
        linhas = f.readlines()
        
    print(f"Total de pacotes capturados: {len(linhas)}")
    
    # Vamos comparar a primeira linha (provavelmente parado) com as outras
    referencia = linhas[0].strip().split()
    
    for i, linha in enumerate(linhas):
        atual = linha.strip().split()
        if len(atual) != len(referencia): continue
        
        # Compara byte a byte
        diferencas = []
        for byte_idx in range(len(referencia)):
            if referencia[byte_idx] != atual[byte_idx]:
                diferencas.append(f"Byte[{byte_idx}]: {referencia[byte_idx]} -> {atual[byte_idx]}")
        
        if diferencas:
            print(f"Linha {i} mudou: {diferencas}")

encontrar_mascara("xbox_dump.txt")