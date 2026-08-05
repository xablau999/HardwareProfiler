/**
 * ========================================================================
 * HARDWARE PROFILER - Ponto de entrada da aplicação
 * ========================================================================
 *
 * POR QUE USAMOS WinMain EM VEZ DE main?
 * ----------------------------------------
 * WinMain é o entry point padrão para aplicações Windows GUI.
 * Diferente de main() que abre um console, WinMain permite criar
 * aplicações que rodam sem janela de console visível.
 *
 * POR QUE USAMOS WS_EX_LAYERED?
 * -----------------------------
 * Permite transparência e efeitos visuais no overlay.
 *
 * POR QUE O OVERLAY É TOPMOST?
 * ----------------------------
 * WS_EX_TOPMOST garante que a janela fique sempre sobre outras,
 * essencial para um overlay de monitoramento.
 */

#include "hardware_profiler.h"
#include "overlay.h"
#include <windows.h>
#include <memory>

 /**
  * WinMain - Entry point da aplicação Windows
  *
  * @param hInstance     Handle da instância atual do programa
  * @param hPrevInstance Handle da instância anterior (sempre NULL)
  * @param lpCmdLine     Linha de comando (não usado)
  * @param nCmdShow      Como a janela deve ser exibida (não usado)
  * @return int          Código de saída (0 = sucesso)
  */
int APIENTRY WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    /**
     * PASSO 1: Inicializar o profiler de hardware
     * -------------------------------------------
     * O profiler roda em uma thread separada e coleta métricas
     * em intervalos regulares (1000ms = 1 segundo).
     *
     * POR QUE USAR UMA THREAD SEPARADA?
     * ---------------------------------
     * Para não bloquear a interface do overlay enquanto coleta
     * dados. Coletar dados de hardware via WMI/PDH pode ser
     * lento e bloquearia a renderização.
     */
    HardwareProfiler profiler;
    profiler.startMonitoring(1000);

    /**
     * PASSO 2: Criar o overlay
     * -------------------------
     * O overlay é a janela que desenha as informações na tela.
     * Usamos unique_ptr para gerenciamento automático de memória
     * (RAII - Resource Acquisition Is Initialization).
     *
     * POR QUE USAR UNIQUE_PTR?
     * ------------------------
     * Garante que o overlay seja destruído automaticamente ao
     * final do escopo, evitando vazamentos de memória.
     */
    auto overlay = std::make_unique<HardwareOverlay>(&profiler);

    /**
     * PASSO 3: Inicializar e executar o overlay
     * ------------------------------------------
     * Se falhar, mostra uma mensagem de erro e encerra.
     */
    if (!overlay->initialize()) {
        MessageBoxW(NULL,
            L"Erro ao inicializar o overlay!\n\n"
            L"Verifique se o sistema atende aos requisitos.",
            L"Hardware Profiler",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    /**
     * PASSO 4: Loop principal
     * ------------------------
     * overlay->run() entra em um loop de mensagens que só termina
     * quando o usuário fecha o programa (ESC, Home, ou X).
     */
    overlay->run();

    /**
     * PASSO 5: Limpeza
     * -----------------
     * Ao sair do loop, para o monitoramento e destrói o overlay
     * automaticamente via unique_ptr.
     */
    profiler.stopMonitoring();

    return 0;
}