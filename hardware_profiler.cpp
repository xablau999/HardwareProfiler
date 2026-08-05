/**
 * ========================================================================
 * HARDWARE PROFILER - Implementação
 * ========================================================================
 *
 * VISÃO GERAL DAS TÉCNICAS UTILIZADAS:
 * ------------------------------------
 * 1. PDH (Performance Data Helper) - Para CPU usage
 *    -> API nativa do Windows para dados de performance
 *
 * 2. WMI (Windows Management Instrumentation) - Para GPU, CPU name, temperatura
 *    -> API abrangente para informações de hardware/software
 *
 * 3. Registry - Fallback para quando WMI falha
 *    -> Leitura direta do registro do Windows
 *
 * 4. GetSystemTimes - Fallback para CPU usage
 *    -> Função nativa que retorna tempos do sistema
 *
 * POR QUE MÚLTIPLOS MÉTODOS?
 * --------------------------
 * Nenhuma API funciona em 100% dos sistemas. Ter fallbacks garante
 * que o programa funcione em praticamente qualquer computador Windows.
 */

#include "hardware_profiler.h"
#include <pdh.h>
#include <psapi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "wbemuuid.lib")

 // ============================================================================
 // CLASSE COM INITIALIZER (RAII para COM)
 // ============================================================================
 // 
 // POR QUE USAMOS RAII PARA COM?
 // -----------------------------
 // COM (Component Object Model) precisa ser inicializado e finalizado
 // corretamente. Usando uma classe com construtor/destrutor, garantimos
 // que isso aconteça automaticamente, mesmo em caso de exceções.

class COMInitializer {
    HRESULT hr;
public:
    COMInitializer() {
        /**
         * CoInitializeEx - Inicializa COM para a thread atual
         * COINIT_MULTITHREADED = Permite que múltiplas threads usem COM
         *
         * POR QUE MULTITHREADED?
         * ----------------------
         * Nossa aplicação tem múltiplas threads (monitor + overlay).
         * COINIT_MULTITHREADED é o mais seguro para este cenário.
         */
        hr = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            /**
             * CoInitializeSecurity - Define nível de segurança COM
             * RPC_C_AUTHN_LEVEL_DEFAULT = Autenticação padrão
             * RPC_C_IMP_LEVEL_IMPERSONATE = Permite impersonação
             *
             * POR QUE PRECISAMOS DISSO?
             * -------------------------
             * WMI requer um nível mínimo de segurança para funcionar.
             */
            HRESULT hrSec = CoInitializeSecurity(NULL, -1, NULL, NULL,
                RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                NULL, EOAC_NONE, NULL);
            (void)hrSec; // Suprime warning de retorno não usado
        }
    }
    ~COMInitializer() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

// ============================================================================
// NOME DA CPU
// ============================================================================

std::string getCPUName() {
    std::string cpuName = "Unknown";

    /**
     * MÉTODO 1: WMI - Win32_Processor
     * -------------------------------
     * Classe WMI que contém informações detalhadas do processador.
     *
     * POR QUE WMI É PREFERÍVEL?
     * -------------------------
     * Retorna o nome completo e formatado do processador,
     * incluindo marca, modelo e frequência.
     */
    IWbemServices* pSvc = NULL;
    IWbemLocator* pLoc = NULL;
    IEnumWbemClassObject* pEnumerator = NULL;

    HRESULT hRes = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hRes)) {
        hRes = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, 0, 0, 0, &pSvc);
        if (SUCCEEDED(hRes)) {
            hRes = pSvc->ExecQuery(bstr_t("WQL"),
                bstr_t("SELECT Name FROM Win32_Processor"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

            if (SUCCEEDED(hRes)) {
                IWbemClassObject* pclsObj = NULL;
                ULONG uReturn = 0;
                while (pEnumerator) {
                    HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                    if (uReturn == 0) break;

                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
                    if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR) {
                        cpuName = ws2s(vtProp.bstrVal);
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

    /**
     * MÉTODO 2: REGISTRO - Fallback
     * -----------------------------
     * Se WMI falhar (sistemas muito antigos ou restritos),
     * tenta ler diretamente do registro do Windows.
     *
     * POR QUE "HARDWARE\DESCRIPTION\System\CentralProcessor\0"?
     * ---------------------------------------------------------
     * Esta é a chave do registro onde o Windows armazena informações
     * do processador primário (índice 0).
     */
    if (cpuName == "Unknown") {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {

            wchar_t buffer[256] = { 0 };
            DWORD size = sizeof(buffer);
            if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
                cpuName = ws2s(buffer);
            }
            RegCloseKey(hKey);
        }
    }

    /**
     * LIMPEZA DO NOME
     * ---------------
     * Remove marcações comerciais e espaços extras
     * para deixar o nome mais limpo e legível.
     */
    if (!cpuName.empty() && cpuName != "Unknown") {
        size_t pos;
        while ((pos = cpuName.find("(TM)")) != std::string::npos) {
            cpuName.erase(pos, 4);
        }
        while ((pos = cpuName.find("(R)")) != std::string::npos) {
            cpuName.erase(pos, 3);
        }
        while ((pos = cpuName.find("  ")) != std::string::npos) {
            cpuName.erase(pos, 1);
        }
        while ((pos = cpuName.find("@ ")) != std::string::npos) {
            size_t end = cpuName.find(" ", pos + 2);
            if (end != std::string::npos) {
                cpuName.erase(pos, end - pos + 1);
            }
            else {
                cpuName.erase(pos);
            }
        }
    }

    return cpuName;
}

// ============================================================================
// CONSTRUTOR/DESTRUTOR
// ============================================================================

HardwareProfiler::HardwareProfiler() {
    /**
     * Coleta dados estáticos (não mudam durante a execução)
     * no construtor para economizar recursos depois.
     */
    cachedGpus = queryGPUInfoWMI();
    cachedOSVersion = queryOSVersion();
    cachedComputerName = queryComputerName();
    updateData();
}

HardwareProfiler::~HardwareProfiler() {
    stopMonitoring();
}

// ============================================================================
// CONTROLE DO MONITORAMENTO
// ============================================================================

void HardwareProfiler::startMonitoring(int intervalMs) {
    if (isMonitoring.load()) return;

    isMonitoring.store(true);
    monitorThread = std::thread([this, intervalMs]() {
        while (isMonitoring.load()) {
            updateData();
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
        });
}

void HardwareProfiler::stopMonitoring() {
    if (isMonitoring.exchange(false)) {
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }
}

HardwareProfiler::SystemInfo HardwareProfiler::getCurrentData() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentData;
}

void HardwareProfiler::updateData() {
    /**
     * POR QUE COLETAMOS FORA DA SEÇÃO CRÍTICA?
     * ---------------------------------------
     * As funções de coleta podem ser lentas (WMI, I/O).
     * Coletamos fora para não bloquear a thread de renderização
     * por mais tempo que o necessário.
     */
    CPUInfo cpu = getCPUInfo();
    RAMInfo ram = getRAMInfo();
    auto disks = getDiskInfo();
    uint64_t uptime = getSystemUptime();

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        currentData.cpu = cpu;
        currentData.ram = ram;
        currentData.disks = std::move(disks);
        currentData.gpus = cachedGpus;
        currentData.osVersion = cachedOSVersion;
        currentData.computerName = cachedComputerName;
        currentData.uptimeSeconds = uptime;
    }
}

// ============================================================================
// CPU - COLETA DE DADOS
// ============================================================================

HardwareProfiler::CPUInfo HardwareProfiler::getCPUInfo() {
    CPUInfo info = {};
    static PDH_HQUERY query = NULL;
    static PDH_HCOUNTER counter = NULL;

    /**
     * MÉTODO 1: PDH (Performance Data Helper)
     * ---------------------------------------
     * API nativa do Windows para dados de performance.
     *
     * POR QUE USAR PDH?
     * -----------------
     * É a forma mais precisa e confiável de obter o uso da CPU
     * no Windows. Usado pelo próprio Task Manager.
     *
     * POR QUE "\\Processor(_Total)\\% Processor Time"?
     * ------------------------------------------------
     * É o contador padrão para uso total da CPU em porcentagem.
     * _Total = Todos os núcleos combinados.
     */
    if (!query) {
        if (PdhOpenQueryW(NULL, 0, &query) == ERROR_SUCCESS) {
            PdhAddCounterW(query, L"\\Processor(_Total)\\% Processor Time", 0, &counter);
            PdhCollectQueryData(query);
        }
    }

    PDH_FMT_COUNTERVALUE value;
    if (query && PdhCollectQueryData(query) == ERROR_SUCCESS) {
        if (PdhGetFormattedCounterValue(counter, PDH_FMT_LONG, NULL, &value) == ERROR_SUCCESS) {
            info.usagePercent = static_cast<int>(value.longValue);
        }
    }

    /**
     * MÉTODO 2: GetSystemTimes - FALLBACK
     * -----------------------------------
     * Se o PDH falhar (sistemas muito antigos), usa GetSystemTimes
     * como alternativa. Calcula o uso da CPU manualmente.
     *
     * POR QUE PRECISAMOS DE FALLBACK?
     * ------------------------------
     * PDH pode falhar em sistemas muito antigos ou com permissões
     * restritas. GetSystemTimes é uma API mais básica que sempre funciona.
     */
    if (info.usagePercent == 0) {
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
                    info.usagePercent = static_cast<int>(((totalDelta - idleDelta) * 100) / totalDelta);
                }
            }
            lastIdle = idleTime;
            lastKernel = kernelTime;
            lastUser = userTime;
        }
    }

    /**
     * FREQUÊNCIA DA CPU
     * -----------------
     * Lê do registro onde o Windows armazena a frequência atual.
     * "~MHz" = Frequência em megahertz.
     */
    HKEY hKey;
    DWORD freq = 0;
    DWORD size = sizeof(freq);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"~MHz", NULL, NULL, (LPBYTE)&freq, &size);
        RegCloseKey(hKey);
        info.frequencyMHz = static_cast<float>(freq);
    }

    /**
     * NÚCLEOS DA CPU
     * --------------
     * GetSystemInfo retorna o número de processadores lógicos
     * (núcleos + hyper-threading).
     */
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    info.coreCount = sysInfo.dwNumberOfProcessors;

    /**
     * NOME DA CPU (cacheado)
     * ----------------------
     * Busca uma vez e reutiliza para sempre.
     * O nome do processador não muda durante a execução.
     */
    static std::string cachedCpuName = "";
    if (cachedCpuName.empty()) {
        cachedCpuName = getCPUName();
    }
    info.model = cachedCpuName;

    return info;
}

// ============================================================================
// RAM - COLETA DE DADOS
// ============================================================================

HardwareProfiler::RAMInfo HardwareProfiler::getRAMInfo() {
    RAMInfo info = {};
    MEMORYSTATUSEX memStatus = { sizeof(memStatus) };

    /**
     * GlobalMemoryStatusEx - API oficial para memória
     * ------------------------------------------------
     * Substitui GlobalMemoryStatus (obsoleta).
     * Suporta mais de 4GB de RAM (usando ullTotalPhys).
     */
    if (GlobalMemoryStatusEx(&memStatus)) {
        info.totalBytes = memStatus.ullTotalPhys;
        info.freeBytes = memStatus.ullAvailPhys;
        info.usedBytes = info.totalBytes - info.freeBytes;
        info.usagePercent = static_cast<int>((info.usedBytes * 100) / info.totalBytes);
    }
    return info;
}

// ============================================================================
// DISCO - COLETA DE DADOS
// ============================================================================

std::vector<HardwareProfiler::DiskInfo> HardwareProfiler::getDiskInfo() {
    std::vector<DiskInfo> disks;
    DWORD drives = GetLogicalDrives();

    /**
     * GetLogicalDrives - Lista todas as unidades do sistema
     * -----------------------------------------------------
     * Retorna um bitmap onde cada bit representa uma letra de unidade.
     * Bit 0 = A:, Bit 1 = B:, etc.
     */
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            wchar_t drive[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
            DiskInfo info;

            info.driveLetter = std::string(1, 'A' + i) + ":\\";

            /**
             * GetDiskFreeSpaceExW - Espaço em disco
             * ------------------------------------
             * Versão Wide (Unicode) da API.
             * Retorna espaço total e livre em bytes.
             */
            ULARGE_INTEGER freeBytes, totalBytes;
            if (GetDiskFreeSpaceExW(drive, &freeBytes, &totalBytes, NULL)) {
                info.totalBytes = totalBytes.QuadPart;
                info.freeBytes = freeBytes.QuadPart;
                info.usagePercent = static_cast<int>(((info.totalBytes - info.freeBytes) * 100) / info.totalBytes);
            }

            /**
             * GetVolumeInformationW - Sistema de arquivos
             * -------------------------------------------
             * Retorna o tipo de sistema de arquivos (NTFS, FAT32, etc).
             */
            wchar_t fsName[MAX_PATH];
            if (GetVolumeInformationW(drive, NULL, 0, NULL, NULL, NULL, fsName, MAX_PATH)) {
                info.fileSystem = ws2s(fsName);
            }

            disks.push_back(info);
        }
    }
    return disks;
}

// ============================================================================
// GPU - COLETA DE DADOS
// ============================================================================

std::vector<HardwareProfiler::GPUInfo> HardwareProfiler::getFallbackGPUInfo() {
    /**
     * FALLBACK PARA GPU - Leitura do Registro
     * ---------------------------------------
     * Se WMI falhar, tenta ler do registro.
     *
     * POR QUE ESTA CHAVE DO REGISTRO?
     * -------------------------------
     * {4d36e968-e325-11ce-bfc1-08002be10318} é o GUID da classe
     * "Display Adapters" no Windows. Todos os drivers de vídeo
     * estão registrados aqui.
     */
    std::vector<GPUInfo> gpus;
    GPUInfo info;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        wchar_t subKey[256];
        for (int i = 0; i < 10; i++) {
            swprintf_s(subKey, _countof(subKey), L"000%d", i);
            HKEY hSubKey;
            if (RegOpenKeyExW(hKey, subKey, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                wchar_t deviceDesc[256] = { 0 };
                DWORD size = sizeof(deviceDesc);
                if (RegQueryValueExW(hSubKey, L"DriverDesc", NULL, NULL, (LPBYTE)deviceDesc, &size) == ERROR_SUCCESS) {
                    info.name = ws2s(deviceDesc);
                    RegCloseKey(hSubKey);
                    break;
                }
                RegCloseKey(hSubKey);
            }
        }
        RegCloseKey(hKey);
    }

    if (info.name.empty()) {
        info.name = "GPU (Standard VGA)";
    }

    // Valor padrão: 1GB (fallback)
    info.memoryTotal = 1024ULL * 1024 * 1024;
    gpus.push_back(info);

    return gpus;
}

std::vector<HardwareProfiler::GPUInfo> HardwareProfiler::queryGPUInfoWMI() {
    std::vector<GPUInfo> gpus;
    COMInitializer com;

    IWbemServices* pSvc = NULL;
    IWbemLocator* pLoc = NULL;
    IEnumWbemClassObject* pEnumerator = NULL;

    if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc)))
        return getFallbackGPUInfo();

    if (SUCCEEDED(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, 0, 0, 0, &pSvc))) {
        if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"),
            bstr_t("SELECT Name, AdapterRAM FROM Win32_VideoController"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator))) {

            IWbemClassObject* pclsObj = NULL;
            ULONG uReturn = 0;

            while (pEnumerator && SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn != 0) {
                GPUInfo info = {};
                VARIANT vtProp;
                VariantInit(&vtProp);

                if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) {
                    info.name = ws2s(vtProp.bstrVal);
                    VariantClear(&vtProp);
                }

                VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"AdapterRAM", 0, &vtProp, 0, 0))) {
                    if (vtProp.vt == VT_UI4) info.memoryTotal = vtProp.ulVal;
                    else if (vtProp.vt == VT_UI8) info.memoryTotal = vtProp.ullVal;
                    VariantClear(&vtProp);
                }

                /**
                 * FILTRO DE GPUS INVÁLIDAS
                 * ------------------------
                 * O WMI pode retornar controladores de vídeo falsos
                 * (como drivers de máquinas virtuais). Filtramos
                 * entradas com nome muito curto ou vazio.
                 */
                if (!info.name.empty() && info.name.length() > 3) {
                    gpus.push_back(info);
                }
                pclsObj->Release();
            }
        }
    }

    if (pEnumerator) pEnumerator->Release();
    if (pSvc) pSvc->Release();
    if (pLoc) pLoc->Release();

    if (gpus.empty()) {
        return getFallbackGPUInfo();
    }

    return gpus;
}

// ============================================================================
// INFORMAÇÕES DO SISTEMA
// ============================================================================

std::string HardwareProfiler::queryOSVersion() {
    /**
     * RtlGetVersion - Versão alternativa para GetVersionEx
     * ----------------------------------------------------
     * GetVersionEx é deprecated no Windows 8+.
     * RtlGetVersion é a API recomendada pela Microsoft para
     * obter a versão do sistema operacional.
     *
     * POR QUE NÃO USAR VERIFYVERSIONINFO?
     * -----------------------------------
     * RtlGetVersion retorna a versão completa, enquanto
     * VerifyVersionInfo apenas verifica se é maior/menor.
     */
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hMod, "RtlGetVersion"));
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
            if (RtlGetVersion(&osvi) == 0) {
                return "Windows " + std::to_string(osvi.dwMajorVersion) + "." +
                    std::to_string(osvi.dwMinorVersion) + " Build " + std::to_string(osvi.dwBuildNumber);
            }
        }
    }
    return "Windows OS";
}

std::string HardwareProfiler::queryComputerName() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(name) / sizeof(wchar_t);
    return GetComputerNameW(name, &size) ? ws2s(name) : "Unknown";
}

uint64_t HardwareProfiler::getSystemUptime() {
    /**
     * GetTickCount64 - Tempo ligado do sistema
     * ----------------------------------------
     * Retorna o número de milissegundos desde a inicialização.
     * GetTickCount (32 bits) pode dar overflow após 49 dias.
     * GetTickCount64 (64 bits) não tem esse problema.
     */
    return GetTickCount64() / 1000;
}

// ============================================================================
// CONVERSÃO DE STRINGS
// ============================================================================

std::wstring s2ws(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string ws2s(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}