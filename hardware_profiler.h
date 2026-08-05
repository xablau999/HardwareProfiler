/**
 * ========================================================================
 * HARDWARE PROFILER - Interface de coleta de dados de hardware
 * ========================================================================
 *
 * OBJETIVO DESTA CLASSE:
 * ----------------------
 * Coletar métricas do sistema em tempo real: CPU, RAM, Disco, GPU,
 * informações do sistema, temperatura, etc.
 *
 * POR QUE USAMOS MUTEX?
 * ---------------------
 * O profiler roda em uma thread separada atualizando os dados,
 * enquanto a thread principal (overlay) lê esses dados.
 * O mutex garante que não haja acesso concorrente aos dados
 * (race condition).
 *
 * POR QUE CACHE DE DADOS ESTÁTICOS?
 * ---------------------------------
 * GPU, versão do Windows e nome do computador dificilmente mudam
 * durante a execução. Buscá-los uma vez via WMI/Registro e cachear
 * economiza recursos e evita chamadas lentas repetidas.
 */

#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

 // ============================================================================
 // FUNÇÕES DE CONVERSÃO DE STRING (ANSI <-> Unicode)
 // ============================================================================
 // 
 // POR QUE PRECISAMOS DISSO?
 // --------------------------
 // Windows usa Unicode (UTF-16) internamente, mas muitas APIs que usamos
 // (como WMI) retornam strings Unicode. Precisamos converter para std::string
 // para facilitar o manuseio no código.
 // 
 // POR QUE USAMOS CP_UTF8?
 // -----------------------
 // UTF-8 é o padrão moderno para textos. Usamos WideCharToMultiByte com
 // CP_UTF8 para preservar caracteres especiais (acentos, etc).

std::wstring s2ws(const std::string& str);
std::string ws2s(const std::wstring& wstr);

// ============================================================================
// CLASSE HARDWARE PROFILER
// ============================================================================

class HardwareProfiler {
public:
    // ========================================================================
    // ESTRUTURAS DE DADOS
    // ========================================================================
    // 
    // POR QUE USAMOS STRUCTS EM VEZ DE CLASSES?
    // -----------------------------------------
    // Structs em C++ têm visibilidade pública por padrão, ideais para
    // estruturas que apenas carregam dados (POD - Plain Old Data).
    // 
    // POR QUE USAMOS uint64_t?
    // ------------------------
    // Para números grandes (bytes de memória/HD), garantindo precisão
    // em todos os sistemas (64 bits = até 18 exabytes).

    struct CPUInfo {
        int usagePercent = 0;      // Uso da CPU em porcentagem (0-100)
        float frequencyMHz = 0;    // Frequência em MHz
        int coreCount = 0;         // Número de núcleos
        std::string model = "Unknown";  // Nome do processador
    };

    struct RAMInfo {
        uint64_t totalBytes = 0;   // Memória total em bytes
        uint64_t usedBytes = 0;    // Memória usada em bytes
        uint64_t freeBytes = 0;    // Memória livre em bytes
        int usagePercent = 0;      // Porcentagem de uso
    };

    struct DiskInfo {
        std::string driveLetter;   // Ex: "C:\"
        uint64_t totalBytes = 0;   // Total em bytes
        uint64_t freeBytes = 0;    // Livre em bytes
        int usagePercent = 0;      // Porcentagem de uso
        std::string fileSystem;    // NTFS, FAT32, etc
    };

    struct GPUInfo {
        std::string name;          // Nome da GPU
        uint64_t memoryTotal = 0;  // VRAM total em bytes
    };

    struct SystemInfo {
        CPUInfo cpu;
        RAMInfo ram;
        std::vector<DiskInfo> disks;
        std::vector<GPUInfo> gpus;
        std::string osVersion;
        std::string computerName;
        uint64_t uptimeSeconds = 0;
    };

private:
    // ========================================================================
    // VARIÁVEIS DE ESTADO
    // ========================================================================
    // 
    // POR QUE USAMOS ATOMIC?
    // ----------------------
    // std::atomic garante operações atômicas (thread-safe) sem a necessidade
    // de mutex. Ideal para flags simples como isMonitoring.
    // 
    // POR QUE USAMOS MUTEX EM VEZ DE ATOMIC PARA OS DADOS?
    // ----------------------------------------------------
    // atomic só funciona para tipos simples. Para estruturas complexas
    // (SystemInfo), precisamos de mutex para garantir consistência.

    std::atomic<bool> isMonitoring{ false };
    std::thread monitorThread;
    SystemInfo currentData;
    mutable std::mutex dataMutex;

    // Cache de dados estáticos (coletados uma vez)
    std::vector<GPUInfo> cachedGpus;
    std::string cachedOSVersion;
    std::string cachedComputerName;

    // ========================================================================
    // MÉTODOS PRIVADOS DE COLETA
    // ========================================================================
    // 
    // POR QUE SEPARAR COLETA E ATUALIZAÇÃO?
    // -------------------------------------
    // Cada método de coleta é independente, permitindo:
    // - Facilidade de testes
    // - Reuso de código
    // - Manutenção simplificada

    CPUInfo getCPUInfo();
    RAMInfo getRAMInfo();
    std::vector<DiskInfo> getDiskInfo();

    /**
     * queryGPUInfoWMI - Busca informações da GPU via WMI
     *
     * POR QUE USAMOS WMI PARA GPU?
     * ----------------------------
     * WMI (Windows Management Instrumentation) é a API oficial da Microsoft
     * para consultar informações de hardware. É mais confiável que ler
     * diretamente do registro ou usar APIs obsoletas.
     *
     * POR QUE TEMOS FALLBACK PARA GPU?
     * ---------------------------------
     * Em alguns sistemas, a WMI pode não retornar informações da GPU
     * (máquinas virtuais, drivers antigos). O fallback tenta ler do
     * registro como alternativa.
     */
    std::vector<GPUInfo> queryGPUInfoWMI();
    std::vector<GPUInfo> getFallbackGPUInfo();

    std::string queryOSVersion();
    std::string queryComputerName();
    uint64_t getSystemUptime();

public:
    HardwareProfiler();
    ~HardwareProfiler();

    void startMonitoring(int intervalMs = 1000);
    void stopMonitoring();
    SystemInfo getCurrentData() const;
    void updateData();
    bool isRunning() const { return isMonitoring.load(); }
};