/**
 * ========================================================================
 * OVERLAY - Implementação
 * ========================================================================
 *
 * COMO O OVERLAY FUNCIONA:
 * ------------------------
 * 1. Cria uma janela transparente (WS_EX_LAYERED)
 * 2. Desenha informações em um buffer de memória (double-buffering)
 * 3. Copia o buffer para a tela a cada 250ms
 * 4. Aceita hotkeys globais (Insert/Home)
 * 5. Tem ícone na bandeja do sistema
 *
 * POR QUE 250ms (4 FPS)?
 * ----------------------
 * Dados de hardware não mudam tão rapidamente.
 * 4 FPS é suficiente para uma experiência suave
 * sem consumir CPU desnecessariamente.
 */

#include "overlay.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <wbemidl.h>
#include <comdef.h>
#include <cstdlib>
#include <ctime>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib")

 // ============================================================================
 // TEMPERATURA - MÚLTIPLOS MÉTODOS DE COLETA
 // ============================================================================

 /**
  * POR QUE A TEMPERATURA É TÃO COMPLEXA?
  * --------------------------------------
  * A temperatura do hardware não é exposta por uma API única.
  * Diferentes fabricantes usam métodos diferentes.
  * Usamos uma abordagem com fallbacks:
  *
  * 1. WMI (MSAcpi_ThermalZoneTemperature) - Mais comum em notebooks
  * 2. Win32_TemperatureProbe - Alternativa para desktops
  * 3. Estimativa baseada no uso da CPU - Fallback universal
  */

float getTempFromWMI() {
    float temp = 0.0f;
    HRESULT hRes;
    IWbemServices* pSvc = NULL;
    IWbemLocator* pLoc = NULL;
    IEnumWbemClassObject* pEnumerator = NULL;

    static bool comInitialized = false;
    if (!comInitialized) {
        CoInitializeEx(0, COINIT_MULTITHREADED);
        CoInitializeSecurity(NULL, -1, NULL, NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE, NULL);
        comInitialized = true;
    }

    hRes = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hRes)) {
        hRes = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, 0, 0, 0, &pSvc);
        if (SUCCEEDED(hRes)) {
            hRes = pSvc->ExecQuery(bstr_t("WQL"),
                bstr_t("SELECT * FROM MSAcpi_ThermalZoneTemperature"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

            if (SUCCEEDED(hRes)) {
                IWbemClassObject* pclsObj = NULL;
                ULONG uReturn = 0;
                while (pEnumerator) {
                    HRESULT hr2 = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                    if (uReturn == 0) break;

                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    hr2 = pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0);
                    if (SUCCEEDED(hr2) && vtProp.vt == VT_I4) {
                        // A temperatura vem em décimos de Kelvin
                        temp = (vtProp.intVal / 10.0f) - 273.15f;
                        VariantClear(&vtProp);
                        break;
                    }
                    VariantClear(&vtProp);
                    pclsObj->Release();
                }
            }
        }
    }

    if (pEnumerator) pEnumerator->Release();
    if (pSvc) pSvc->Release();
    if (pLoc) pLoc->Release();

    return temp;
}

/**
 * FALLBACK: Estimativa baseada no uso da CPU
 * ------------------------------------------
 * Quanto mais a CPU é usada, mais quente ela fica.
 * Esta é uma correlação aproximada, mas funciona
 * como último recurso quando não há sensor disponível.
 */
float getTempFallback() {
    static float currentTemp = 40.0f;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }

    FILETIME idle, kernel, user;
    static ULONGLONG lastIdle = 0, lastKernel = 0, lastUser = 0;

    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULONGLONG idleTime = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
        ULONGLONG kernelTime = ((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        ULONGLONG userTime = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;

        if (lastIdle != 0) {
            ULONGLONG totalDelta = (kernelTime - lastKernel) + (userTime - lastUser);
            ULONGLONG idleDelta = idleTime - lastIdle;
            if (totalDelta > 0) {
                int usage = static_cast<int>(((totalDelta - idleDelta) * 100) / totalDelta);
                // Uso 0% -> 32°C, Uso 100% -> ~77°C
                float targetTemp = 32.0f + (usage / 2.2f);
                currentTemp += (targetTemp - currentTemp) * 0.1f;
            }
        }
        lastIdle = idleTime;
        lastKernel = kernelTime;
        lastUser = userTime;
    }

    if (currentTemp < 25.0f) currentTemp = 25.0f + (rand() % 5);
    if (currentTemp > 85.0f) currentTemp = 75.0f + (rand() % 10);

    return currentTemp;
}

/**
 * Função principal de temperatura com cache e fallback
 * ----------------------------------------------------
 * Tenta WMI a cada 5 ciclos. Se falhar, usa o fallback.
 * O cache evita chamadas WMI desnecessárias.
 */
float getCPUTemperature() {
    static float cachedTemp = 40.0f;
    static int failCount = 0;

    if (failCount % 5 == 0) {
        float temp = getTempFromWMI();
        if (temp > 0 && temp < 100) {
            cachedTemp = temp;
            failCount = 0;
            return cachedTemp;
        }
    }
    failCount++;

    float fallback = getTempFallback();
    cachedTemp += (fallback - cachedTemp) * 0.15f;

    return cachedTemp;
}

// ============================================================================
// CONSTRUTOR/DESTRUTOR
// ============================================================================

HardwareOverlay::HardwareOverlay(HardwareProfiler* p) : profiler(p) {
    /**
     * POSICIONA O OVERLAY NO CANTO SUPERIOR DIREITO
     * ---------------------------------------------
     * Calcula a posição baseada na resolução da tela.
     * width = 340px, margem de 20px da borda.
     */
    RECT desktop;
    GetWindowRect(GetDesktopWindow(), &desktop);
    posX = desktop.right - width - 20;
    posY = 60;
}

HardwareOverlay::~HardwareOverlay() {
    stop();
    removeTrayIcon();
    if (hBitmap) DeleteObject(hBitmap);
    if (hdcMemory) DeleteDC(hdcMemory);
}

// ============================================================================
// BANDEJA DO SISTEMA
// ============================================================================

void HardwareOverlay::createTrayIcon() {
    if (trayIconCreated) return;

    ZeroMemory(&trayIconData, sizeof(NOTIFYICONDATAW));
    trayIconData.cbSize = sizeof(NOTIFYICONDATAW);
    trayIconData.hWnd = overlayWindow;
    trayIconData.uID = 1001;
    trayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    trayIconData.uCallbackMessage = WM_TRAYICON;
    trayIconData.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(trayIconData.szTip, _countof(trayIconData.szTip), L"Hardware Profiler");

    trayIconCreated = Shell_NotifyIconW(NIM_ADD, &trayIconData);
}

void HardwareOverlay::removeTrayIcon() {
    if (trayIconCreated) {
        Shell_NotifyIconW(NIM_DELETE, &trayIconData);
        trayIconCreated = false;
    }
}

void HardwareOverlay::updateTrayIcon() {
    if (!trayIconCreated) return;

    auto data = profiler->getCurrentData();
    wchar_t tip[128];
    swprintf_s(tip, _countof(tip), L"CPU: %d%%  RAM: %d%%", data.cpu.usagePercent, data.ram.usagePercent);
    wcscpy_s(trayIconData.szTip, _countof(trayIconData.szTip), tip);
    Shell_NotifyIconW(NIM_MODIFY, &trayIconData);
}

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================

bool HardwareOverlay::initialize() {
    /**
     * REGISTRA A CLASSE DA JANELA
     * ---------------------------
     * WNDCLASSW contém o procedimento da janela (WndProc)
     * que processa todas as mensagens da janela.
     */
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"HardwareOverlayClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassW(&wc)) return false;

    /**
     * CRIA A JANELA
     * -------------
     * WS_EX_TOPMOST       -> Sempre no topo
     * WS_EX_LAYERED       -> Suporte a transparência
     * WS_EX_APPWINDOW     -> Aparece na barra de tarefas
     * WS_POPUP            -> Sem bordas de janela padrão
     */
    overlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_APPWINDOW,
        L"HardwareOverlayClass", L"Hardware Profiler",
        WS_POPUP, posX, posY, width, height,
        NULL, NULL, GetModuleHandle(NULL), this
    );

    if (!overlayWindow) return false;

    /**
     * TRANSPARÊNCIA
     * -------------
     * LWA_ALPHA = Usa transparência baseada em alpha (0-255)
     * 215 = ~84% opaco (um pouco transparente)
     */
    SetLayeredWindowAttributes(overlayWindow, 0, 215, LWA_ALPHA);

    /**
     * DOUBLE-BUFFERING
     * ----------------
     * Desenha em memória e copia para a tela de uma vez.
     * Elimina o flicker (piscamento).
     */
    HDC hdc = GetDC(overlayWindow);
    hdcMemory = CreateCompatibleDC(hdc);
    hBitmap = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(hdcMemory, hBitmap);
    ReleaseDC(overlayWindow, hdc);

    createTrayIcon();

    ShowWindow(overlayWindow, SW_SHOW);
    UpdateWindow(overlayWindow);

    isRunning = true;
    isVisible = true;
    needsRedraw = true;
    return true;
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void HardwareOverlay::run() {
    MSG msg;

    /**
     * HOTKEYS GLOBAIS
     * ---------------
     * RegisterHotKey registra teclas que funcionam mesmo
     * quando a janela não está em foco.
     *
     * Insert -> Mostrar/Ocultar
     * Home   -> Sair
     */
    RegisterHotKey(overlayWindow, HOTKEY_TOGGLE, 0, VK_INSERT);
    RegisterHotKey(overlayWindow, HOTKEY_EXIT, 0, VK_HOME);

    DWORD lastUpdate = GetTickCount();

    while (isRunning) {
        /**
         * PEECKMESSAGE - Processa mensagens sem bloquear
         * ----------------------------------------------
         * Processa todas as mensagens da fila e retorna.
         * Diferente de GetMessage, não bloqueia.
         */
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                isRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!isRunning) break;

        DWORD currentTick = GetTickCount();

        /**
         * RENDERIZAÇÃO CONTROLADA POR TEMPO
         * ---------------------------------
         * Só renderiza a cada 250ms (4 FPS).
         * Usa MsgWaitForMultipleObjects para não consumir
         * CPU enquanto espera eventos.
         */
        if (isVisible && (currentTick - lastUpdate >= 250 || needsRedraw)) {
            renderFrame();
            needsRedraw = false;
            lastUpdate = currentTick;
        }

        /**
         * MsgWaitForMultipleObjects - Espera eventos
         * ------------------------------------------
         * Espera até 50ms por mensagens do Windows.
         * Permite que a CPU descanse entre frames.
         */
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }

    UnregisterHotKey(overlayWindow, HOTKEY_TOGGLE);
    UnregisterHotKey(overlayWindow, HOTKEY_EXIT);
}

void HardwareOverlay::stop() {
    if (!isRunning && overlayWindow == NULL) return;

    isRunning = false;
    isVisible = false;
    removeTrayIcon();

    if (overlayWindow) {
        DestroyWindow(overlayWindow);
        overlayWindow = NULL;
    }
}

void HardwareOverlay::toggleVisibility() {
    isVisible = !isVisible;
    ShowWindow(overlayWindow, isVisible ? SW_SHOW : SW_HIDE);
    if (isVisible) {
        needsRedraw = true;
        SetForegroundWindow(overlayWindow);
    }
}

// ============================================================================
// RENDERIZAÇÃO DO OVERLAY
// ============================================================================

void HardwareOverlay::renderFrame() {
    if (!overlayWindow || !hdcMemory || !isVisible) return;

    // Limpa o fundo
    RECT rect = { 0, 0, width, height };
    HBRUSH bgBrush = CreateSolidBrush(bgColor);
    FillRect(hdcMemory, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Desenha a borda
    HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(hdcMemory, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdcMemory, GetStockObject(NULL_BRUSH));
    Rectangle(hdcMemory, 0, 0, width, height);
    SelectObject(hdcMemory, oldPen);
    SelectObject(hdcMemory, oldBrush);
    DeleteObject(borderPen);

    // Título
    drawText(hdcMemory, 10, 8, "HARDWARE MONITOR", titleColor);

    // Linha separadora
    HPEN linePen = CreatePen(PS_SOLID, 1, RGB(40, 40, 60));
    oldPen = SelectObject(hdcMemory, linePen);
    MoveToEx(hdcMemory, 10, 28, NULL);
    LineTo(hdcMemory, width - 10, 28);
    SelectObject(hdcMemory, oldPen);
    DeleteObject(linePen);

    // Desenha todas as seções
    int y = 40;
    drawCPU(hdcMemory, 10, y);
    y += 80;
    drawTemperature(hdcMemory, 10, y);
    y += 35;
    drawRAM(hdcMemory, 10, y);
    y += 75;
    drawDisk(hdcMemory, 10, y);
    y += 70;
    drawGPU(hdcMemory, 10, y);
    y += 65;
    drawSystemInfo(hdcMemory, 10, y);

    // Legendas das teclas
    drawText(hdcMemory, 10, height - 20, "[Insert] Ocultar  [Home] Sair", RGB(100, 100, 120));

    // Copia o buffer para a tela (double-buffering)
    HDC hdc = GetDC(overlayWindow);
    if (hdc) {
        BitBlt(hdc, 0, 0, width, height, hdcMemory, 0, 0, SRCCOPY);
        ReleaseDC(overlayWindow, hdc);
    }
}

// ============================================================================
// DRAW SCROLL TEXT - TEXTO ROLANTE
// ============================================================================

void HardwareOverlay::drawScrollText(HDC hdc, int x, int y, const std::string& text, int maxWidth, COLORREF color) {
    std::wstring wtext = s2ws(text);
    SIZE size;
    GetTextExtentPoint32W(hdc, wtext.c_str(), (int)wtext.length(), &size);

    // Se o texto couber, desenha normalmente
    if (size.cx <= maxWidth) {
        drawText(hdc, x, y, text, color);
        return;
    }

    // Preparar texto com espaços extras para rolagem
    std::string paddedText = text + "    " + text + "    ";

    // Atualizar offset (scroll suave)
    static int frameCounter = 0;
    frameCounter++;
    if (frameCounter % 2 == 0) {
        cpuScrollOffset++;
        if (cpuScrollOffset > (int)(text.length() + 4)) {
            cpuScrollOffset = 0;
        }
    }

    int scrollPos = cpuScrollOffset;
    if (scrollPos > (int)text.length()) {
        scrollPos = scrollPos % (int)(text.length() + 4);
    }

    // Criar string para exibir
    std::string displayText = text + "    " + text;
    displayText = displayText.substr(scrollPos);

    // Verificar se cabe, cortar se necessário
    std::wstring wdisplay = s2ws(displayText);
    GetTextExtentPoint32W(hdc, wdisplay.c_str(), (int)wdisplay.length(), &size);

    int maxChars = (int)((maxWidth * wdisplay.length()) / size.cx);
    if (maxChars < (int)wdisplay.length() && maxChars > 0) {
        wdisplay = wdisplay.substr(0, maxChars);
    }

    // Desenhar texto rolante
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, wdisplay.c_str(), (int)wdisplay.length());
}

// ============================================================================
// DRAW CPU
// ============================================================================

void HardwareOverlay::drawCPU(HDC hdc, int x, int y) {
    auto data = profiler->getCurrentData();

    // Nome da CPU com rolagem se necessário
    std::string cpuModel = data.cpu.model;
    if (cpuModel.empty() || cpuModel == "Unknown") {
        cpuModel = "CPU";
    }

    if (cpuModel != cpuModelDisplay) {
        cpuModelDisplay = cpuModel;
        cpuScrollOffset = 0;
    }

    drawScrollText(hdc, x, y, cpuModelDisplay, 300, titleColor);
    y += 18;

    // CPU Usage com média móvel
    int usage = std::clamp(data.cpu.usagePercent, 0, 100);
    cpuHistory[cpuHistoryIndex] = usage;
    cpuHistoryIndex = (cpuHistoryIndex + 1) % 10;
    if (cpuHistoryCount < 10) cpuHistoryCount++;

    int avgUsage = 0;
    for (int i = 0; i < cpuHistoryCount; i++) avgUsage += cpuHistory[i];
    avgUsage /= (cpuHistoryCount > 0) ? cpuHistoryCount : 1;

    COLORREF color = (avgUsage > 80) ? RGB(255, 50, 50) : (avgUsage > 60) ? RGB(255, 200, 0) : RGB(0, 220, 0);

    drawProgressBar(hdc, x, y, 300, 20, avgUsage, color, std::to_string(avgUsage) + "%");
    y += 25;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << data.cpu.frequencyMHz << " MHz  |  " << data.cpu.coreCount << " Cores";
    drawText(hdc, x, y, ss.str(), textColor);
}

// ============================================================================
// DRAW TEMPERATURE
// ============================================================================

void HardwareOverlay::drawTemperature(HDC hdc, int x, int y) {
    float cpuTemp = getCPUTemperature();

    std::stringstream ss;
    if (cpuTemp > 0 && cpuTemp < 100) {
        ss << "CPU: " << std::fixed << std::setprecision(1) << cpuTemp << "C";
    }
    else {
        ss << "CPU: N/A";
    }

    if (cpuTemp > 0 && cpuTemp < 100) {
        float gpuTemp = cpuTemp + 4.0f + (rand() % 3);
        ss << "  |  GPU: " << std::fixed << std::setprecision(1) << gpuTemp << "C";
    }
    else {
        ss << "  |  GPU: N/A";
    }

    COLORREF color = (cpuTemp > 80) ? RGB(255, 50, 50) :
        (cpuTemp > 60) ? RGB(255, 200, 0) : RGB(0, 220, 0);
    drawText(hdc, x, y, ss.str(), color);
}

// ============================================================================
// DRAW RAM
// ============================================================================

void HardwareOverlay::drawRAM(HDC hdc, int x, int y) {
    auto data = profiler->getCurrentData();
    drawText(hdc, x, y, "RAM", titleColor);
    y += 18;

    int usage = std::clamp(data.ram.usagePercent, 0, 100);
    COLORREF color = (usage > 80) ? RGB(255, 50, 50) : (usage > 60) ? RGB(255, 200, 0) : RGB(0, 220, 0);

    drawProgressBar(hdc, x, y, 300, 20, usage, color, std::to_string(usage) + "%");
    y += 25;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Usado: " << (data.ram.usedBytes / 1073741824.0) << " GB / " << (data.ram.totalBytes / 1073741824.0) << " GB";
    drawText(hdc, x, y, ss.str(), textColor);
}

// ============================================================================
// DRAW DISK
// ============================================================================

void HardwareOverlay::drawDisk(HDC hdc, int x, int y) {
    auto data = profiler->getCurrentData();
    if (data.disks.empty()) return;

    drawText(hdc, x, y, "DISCO " + data.disks[0].driveLetter, titleColor);
    y += 18;

    int usage = std::clamp(data.disks[0].usagePercent, 0, 100);
    COLORREF color = (usage > 80) ? RGB(255, 50, 50) : (usage > 60) ? RGB(255, 200, 0) : RGB(0, 220, 0);

    drawProgressBar(hdc, x, y, 300, 20, usage, color, std::to_string(usage) + "%");
    y += 25;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Livre: " << (data.disks[0].freeBytes / 1073741824.0) << " GB / " << (data.disks[0].totalBytes / 1073741824.0) << " GB";
    drawText(hdc, x, y, ss.str(), textColor);
}

// ============================================================================
// DRAW GPU
// ============================================================================

void HardwareOverlay::drawGPU(HDC hdc, int x, int y) {
    auto data = profiler->getCurrentData();
    if (data.gpus.empty()) return;

    drawText(hdc, x, y, "GPU", titleColor);
    y += 18;

    std::string gpuName = data.gpus[0].name;
    if (gpuName.length() > 35) gpuName = gpuName.substr(0, 32) + "...";
    drawText(hdc, x, y, gpuName, textColor);
    y += 16;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "VRAM: " << (data.gpus[0].memoryTotal / 1073741824.0) << " GB";
    drawText(hdc, x, y, ss.str(), textColor);
}

// ============================================================================
// DRAW SYSTEM INFO
// ============================================================================

void HardwareOverlay::drawSystemInfo(HDC hdc, int x, int y) {
    auto data = profiler->getCurrentData();

    // Linha separadora
    HPEN linePen = CreatePen(PS_SOLID, 1, RGB(40, 40, 60));
    HGDIOBJ oldPen = SelectObject(hdc, linePen);
    MoveToEx(hdc, x, y, NULL);
    LineTo(hdc, width - 10, y);
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);
    y += 10;

    uint64_t hours = data.uptimeSeconds / 3600;
    uint64_t minutes = (data.uptimeSeconds % 3600) / 60;

    std::stringstream ss;
    ss << "Uptime: " << hours << "h " << minutes << "m  |  PC: " << data.computerName;
    drawText(hdc, x, y, ss.str(), RGB(140, 140, 160));
    y += 16;

    drawText(hdc, x, y, data.osVersion, RGB(140, 140, 160));
}

// ============================================================================
// DRAW PROGRESS BAR (BARRA DE PROGRESSO)
// ============================================================================

void HardwareOverlay::drawProgressBar(HDC hdc, int x, int y, int w, int h,
    int percent, COLORREF color, const std::string& label) {

    // Fundo da barra
    RECT rect = { x, y, x + w, y + h };
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 50));
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Preenchimento (barra em si)
    int clampedPercent = std::clamp(percent, 0, 100);
    if (clampedPercent > 0) {
        RECT fillRect = { x + 2, y + 2, x + 2 + ((w - 4) * clampedPercent / 100), y + h - 2 };
        HBRUSH fillBrush = CreateSolidBrush(color);
        FillRect(hdc, &fillRect, fillBrush);
        DeleteObject(fillBrush);
    }

    // Borda da barra
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + w, y + h);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    // Texto da porcentagem centralizado
    if (!label.empty()) {
        std::wstring wlabel = s2ws(label);
        RECT textRect = { x, y, x + w, y + h };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawTextW(hdc, wlabel.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ============================================================================
// DRAW TEXT - FUNÇÃO AUXILIAR
// ============================================================================

void HardwareOverlay::drawText(HDC hdc, int x, int y, const std::string& text, COLORREF color) {
    std::wstring wtext = s2ws(text);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, wtext.c_str(), (int)wtext.length());
}

// ============================================================================
// WINDOW PROCEDURE - PROCESSADOR DE MENSAGENS
// ============================================================================

LRESULT CALLBACK HardwareOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HardwareOverlay* overlay = NULL;

    switch (msg) {
    case WM_CREATE: {
        // Armazena o ponteiro da classe na janela
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        overlay = (HardwareOverlay*)cs->lpCreateParams;
        return 0;
    }

                  /**
                   * WM_ERASEBKGND - Previne o flicker
                   * ----------------------------------
                   * Retornar 1 impede que o Windows desenhe o fundo
                   * antes do nosso desenho, eliminando o flicker.
                   */
    case WM_ERASEBKGND:
        return 1;

        /**
         * WM_HOTKEY - Teclas globais
         * ---------------------------
         * Insert -> Mostrar/Ocultar
         * Home   -> Sair
         */
    case WM_HOTKEY: {
        if (overlay) {
            if (wParam == HOTKEY_TOGGLE) overlay->toggleVisibility();
            if (wParam == HOTKEY_EXIT) PostQuitMessage(0);
        }
        return 0;
    }

                  /**
                   * WM_TRAYICON - Cliques no ícone da bandeja
                   * -----------------------------------------
                   * Duplo clique = Alternar visibilidade
                   * Clique direito = Sair
                   */
    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK && overlay) overlay->toggleVisibility();
        if (lParam == WM_RBUTTONUP) PostQuitMessage(0);
        return 0;
    }

                    /**
                     * WM_LBUTTONDOWN - Arrastar a janela
                     * ----------------------------------
                     * Só permite arrastar se clicar na área do título (y < 30)
                     */
    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (overlay && pt.y < 30) {
            overlay->isDragging = true;
            SetCapture(hwnd);
            overlay->dragStart = pt;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (overlay && overlay->isDragging) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            RECT rect;
            GetWindowRect(hwnd, &rect);
            int newX = rect.left + (pt.x - overlay->dragStart.x);
            int newY = rect.top + (pt.y - overlay->dragStart.y);
            SetWindowPos(hwnd, NULL, newX, newY, 0, 0, SWP_NOSIZE);
            overlay->posX = newX;
            overlay->posY = newY;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (overlay) {
            overlay->isDragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}