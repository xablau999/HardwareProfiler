/**
 * ========================================================================
 * OVERLAY - Interface da janela de sobreposição (overlay)
 * ========================================================================
 *
 * OBJETIVO:
 * --------
 * Criar uma janela transparente que se sobrepõe a todas as outras
 * e desenha informações de hardware em tempo real.
 *
 * POR QUE USAMOS OVERLAY EM VEZ DE UMA JANELA COMUM?
 * ---------------------------------------------------
 * 1. Transparência: Permite ver o que está por baixo
 * 2. Sempre no topo: Não é coberto por outras janelas
 * 3. Não intrusivo: Não atrapalha o uso normal do computador
 *
 * POR QUE USAMOS GDI EM VEZ DE DIRECTX/OPENGL?
 * --------------------------------------------
 * GDI é mais leve e suficiente para desenhar textos e barras.
 * DirectX/OpenGL seriam overkill para esta aplicação.
 */

#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <string>
#include "hardware_profiler.h"

 // ============================================================================
 // CONSTANTES
 // ============================================================================

#define HOTKEY_TOGGLE 1      // Insert - Mostrar/Ocultar
#define HOTKEY_EXIT 2        // Home - Sair
#define WM_TRAYICON (WM_USER + 1)  // Mensagem personalizada para bandeja

// ============================================================================
// CLASSE HARDWARE OVERLAY
// ============================================================================

class HardwareOverlay {
private:
    // ========================================================================
    // RECURSOS WINDOWS
    // ========================================================================
    // 
    // HWND - Handle da janela (identificador único)
    // HDC - Device Context (contexto de desenho)
    // HBITMAP - Bitmap para double-buffering
    // 
    // POR QUE PRECISAMOS DE HDC MEMORY E HBITMAP?
    // -------------------------------------------
    // Double-buffering: Desenhamos em memória e depois copiamos
    // para a tela de uma vez, eliminando o flicker (piscamento).

    HWND overlayWindow = NULL;
    HDC hdcMemory = NULL;
    HBITMAP hBitmap = NULL;

    // ========================================================================
    // DIMENSÕES E ESTADO
    // ========================================================================

    int width = 340;
    int height = 460;
    bool isVisible = true;      // Overlay visível ou oculto
    bool isRunning = false;     // Loop principal ativo
    bool needsRedraw = true;    // Força redesenho na próxima iteração

    int posX = 0, posY = 0;
    bool isDragging = false;
    POINT dragStart = { 0, 0 };

    HardwareProfiler* profiler = nullptr;

    // ========================================================================
    // CORES DO TEMA
    // ========================================================================
    // 
    // POR QUE USAMOS CORES ESCURAS?
    // -----------------------------
    // Fundo escuro contrasta melhor com texto claro,
    // e a transparência fica mais elegante.

    COLORREF bgColor = RGB(15, 15, 30);      // Azul muito escuro
    COLORREF borderColor = RGB(0, 120, 215); // Azul vibrante (borda)
    COLORREF textColor = RGB(200, 200, 220); // Branco acizentado
    COLORREF titleColor = RGB(0, 180, 255);  // Azul claro (títulos)

    // ========================================================================
    // HISTÓRICO DA CPU (MÉDIA MÓVEL)
    // ========================================================================
    // 
    // POR QUE USAMOS MÉDIA MÓVEL?
    // ---------------------------
    // O uso da CPU pode oscilar muito entre leituras.
    // A média móvel suaviza as variações, mostrando uma tendência
    // mais estável e fácil de ler.

    int cpuHistory[10] = { 0 };
    int cpuHistoryIndex = 0;
    int cpuHistoryCount = 0;

    // ========================================================================
    // TEXTO ROLANTE (MARQUEE) PARA CPU
    // ========================================================================
    // 
    // POR QUE TEXTO ROLANTE?
    // ----------------------
    // Nomes de processadores podem ser muito longos para a janela.
    // Em vez de cortar ou usar fonte menor, o texto rola
    // horizontalmente, mostrando o nome completo.

    std::string cpuModelDisplay;
    int cpuScrollOffset = 0;
    int cpuScrollTimer = 0;

    // ========================================================================
    // BANDEJA DO SISTEMA (SYSTEM TRAY)
    // ========================================================================
    // 
    // POR QUE USAR BANDEJA?
    // ---------------------
    // Permite que o programa fique rodando em segundo plano
    // sem ocupar espaço na barra de tarefas.
    // O usuário pode restaurar a janela com duplo clique.

    NOTIFYICONDATAW trayIconData = {};
    bool trayIconCreated = false;

    // ========================================================================
    // MÉTODOS
    // ========================================================================

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void drawCPU(HDC hdc, int x, int y);
    void drawRAM(HDC hdc, int x, int y);
    void drawDisk(HDC hdc, int x, int y);
    void drawGPU(HDC hdc, int x, int y);
    void drawSystemInfo(HDC hdc, int x, int y);
    void drawTemperature(HDC hdc, int x, int y);

    void drawProgressBar(HDC hdc, int x, int y, int w, int h,
        int percent, COLORREF color, const std::string& label);
    void drawText(HDC hdc, int x, int y, const std::string& text, COLORREF color = RGB(200, 200, 220));

    /**
     * drawScrollText - Desenha texto com rolagem horizontal
     *
     * POR QUE ISSO É NECESSÁRIO?
     * --------------------------
     * Nomes de processadores (ex: "Intel Core i7-9700K @ 3.60GHz")
     * podem ter até 40+ caracteres. Nossa janela tem apenas 340px
     * de largura. Esta função detecta se o texto cabe e, se não,
     * cria uma animação de rolagem suave.
     */
    void drawScrollText(HDC hdc, int x, int y, const std::string& text, int maxWidth, COLORREF color);

    void createTrayIcon();
    void removeTrayIcon();
    void updateTrayIcon();

    void renderFrame();

public:
    HardwareOverlay(HardwareProfiler* p);
    ~HardwareOverlay();

    bool initialize();
    void run();
    void stop();
    void toggleVisibility();
    void forceRedraw() { needsRedraw = true; }
};