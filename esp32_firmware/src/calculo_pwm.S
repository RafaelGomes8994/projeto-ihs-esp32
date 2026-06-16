// =============================================================================
// calculo_pwm.S — Cálculo de Duty Cycle PWM em Assembly Xtensa LX6
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// CORREÇÃO CRÍTICA — Windowed ABI vs CALL0 ABI
// ─────────────────────────────────────────────
// O Arduino/ESP32 compila C/C++ com a WINDOWED ABI do Xtensa (padrão GCC).
// Toda função Assembly que é chamada por código C DEVE obedecer:
//
//   1. ENTRY A1, N    — Rotaciona a janela de registradores e aloca N bytes
//                        de stack. SEM isso, o caller perde seus registradores.
//
//   2. RETW.N         — Restaura a janela de registradores e retorna.
//                        NUNCA use RET.N com Windowed ABI: ele salta para A0
//                        sem restaurar a janela, corrompendo o estado do caller
//                        → Guru Meditation InstrFetchProhibited.
//
// Diagnóstico do crash anterior:
//   EXCCAUSE=0x14 (InstrFetchProhibited) + A0==PC==0x800e7ad6 (inválido)
//   Causa: RET.N saltou para A0 sem restaurar a janela. O A0 visível no
//   contexto corrompido não era o return address real, era lixo da janela
//   anterior. O CPU tentou executar de 0x800e7ad6 → pânico.
//
// Função: uint32_t calcularPWM(uint8_t entrada)
//   Lógica: duty = entrada * 4
//   Mapeia 0-255 para 0-1020 (≈ resolução 10-bit do ledc, range 0-1023)
//   Exemplo: calcularPWM(200) = 800 = 78% de duty cycle
//
// Windowed ABI — fluxo de chamada CALL4:
//   Caller:  a2 = argumento; CALL4 calcularPWM
//   CPU:     rotaciona janela +4; A0_novo = return_address
//   Callee:  ENTRY a1, 16 → aloca stack; A2 = argumento (preservado na rotação)
//   Callee:  opera em A2, A3 (scratch da nova janela, não afeta o caller)
//   Callee:  RETW.N → restaura janela; A2_caller = A2_callee (valor de retorno)
// =============================================================================

    .global calcularPWM
    .type   calcularPWM, @function
    .section .text.calcularPWM, "ax", @progbits

calcularPWM:
    // ------------------------------------------------------------------
    // ENTRY: obrigatório na Windowed ABI.
    // Rotaciona a janela de registradores e aloca 16 bytes de stack frame.
    // 16 bytes é o mínimo exigido pelo ABI (alinhamento de 16 bytes).
    // ------------------------------------------------------------------
    entry   a1, 16

    // ------------------------------------------------------------------
    // Isola os 8 bits baixos do argumento 'entrada' (a2).
    // Necessário porque o caller pode passar um uint8_t em um registrador
    // de 32 bits com bits superiores nao-zerados dependendo do contexto.
    // EXTUI: extrai bits [offset+len-1 : offset] de AS e zero-estende para 32b
    // ------------------------------------------------------------------
    extui   a2, a2, 0, 8        // a2 = a2[7:0]

    // ------------------------------------------------------------------
    // Multiplicacao: duty = entrada * 4
    // MULL AR, AS, AT: AR = (AS * AT) & 0xFFFFFFFF (32 bits baixos)
    // a3 e scratch — nao afeta o caller (janela isolada pelo ENTRY)
    // ------------------------------------------------------------------
    movi    a3, 4               // a3 = 4
    mull    a2, a2, a3          // a2 = a2 * 4  (retorno em a2)

    // ------------------------------------------------------------------
    // RETW.N: retorno com restauracao da janela (Windowed ABI).
    // Restaura a janela do caller; o valor de retorno fica em a2.
    // .N = narrow encoding (2 bytes em vez de 3, economiza flash).
    // ------------------------------------------------------------------
    retw.n

    .size calcularPWM, .-calcularPWM