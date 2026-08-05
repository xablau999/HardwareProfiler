# 🖥️ HardwareProfiler

![Windows](https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![C++](https://img.shields.io/badge/C++-17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-Complete-success?style=for-the-badge)

## 📊 Sobre o Projeto

**HardwareProfiler** é uma ferramenta de monitoramento de hardware em tempo real para Windows, desenvolvida em C++ com overlay GDI. Exibe métricas críticas do sistema de forma não-intrusiva e profissional, similar a overlays de jogos (MSI Afterburner, RivaTuner).

### ✨ Funcionalidades

- 📈 **Monitoramento em tempo real**: CPU (uso, frequência, núcleos, nome), RAM (uso, total, usado), Disco (uso, espaço livre), GPU (nome, VRAM)
- 🌡️ **Temperatura**: Leitura via WMI com fallback inteligente
- 🎨 **Overlay GDI**: Janela transparente e sempre no topo (WS_EX_TOPMOST)
- 🖱️ **Arrastável**: Clique e arraste para posicionar onde quiser
- 🔘 **Bandeja do Sistema**: Ícone na bandeja com duplo clique para mostrar/ocultar
- ⌨️ **Hotkeys Globais**: `Insert` para mostrar/ocultar, `Home` para sair
- 📜 **Texto Rolante**: Nome do processador rola horizontalmente se for muito longo
- ⚡ **Baixo consumo**: Otimizado para rodar em background com 4 FPS

## 📸 Screenshots

![HardwareProfiler em ação](screenshot.png)

### 🛠️ Tecnologias Utilizadas

| Tecnologia | Finalidade |
|------------|------------|
| **C++17** | Linguagem principal |
| **Windows API (Win32)** | Criação de janelas, GDI, mensagens |
| **PDH (Performance Data Helper)** | Leitura de uso da CPU |
| **WMI (Windows Management Instrumentation)** | Leitura de GPU, temperatura, nome da CPU |
| **GDI (Graphics Device Interface)** | Renderização do overlay |
| **Double-buffering** | Eliminação de flicker |

## 🏗️ Arquitetura do Projeto

### Estrutura de Arquivos
HardwareProfiler/
├── main.cpp # Ponto de entrada (WinMain)
├── hardware_profiler.h/cpp # Coleta de métricas (CPU, RAM, Disco, GPU, Temp)
├── overlay.h/cpp # Overlay GDI, renderização, interação
├── README.md # Documentação
├── LICENSE # MIT License
└── HardwareProfiler.sln # Projeto Visual Studio

### Fluxo de Dados (Simplificado)
┌─────────────────────────────────────────────────────────┐
│ HARDWARE PROFILER │
├─────────────────────────────────────────────────────────┤
│ │
│ ┌─────────────────┐ ┌────────────────────────────┐ │
│ │ Thread (1s) │ │ Overlay (4 FPS) │ │
│ │ updateData() │───▶│ renderFrame() │ │
│ └─────────────────┘ └────────────────────────────┘ │
│ │ │ │
│ ▼ ▼ │
│ ┌─────────────────┐ ┌────────────────────────────┐ │
│ │ Coleta de Dados │ │ Renderização GDI │ │
│ │ • CPU (PDH) │ │ • CPU (nome + uso) │ │
│ │ • RAM (WMI) │ │ • RAM (uso + total) │ │
│ │ • Disco (API) │ │ • Disco (uso + livre) │ │
│ │ • GPU (WMI) │ │ • GPU (nome + VRAM) │ │
│ │ • Temp (WMI) │ │ • Temperatura (CPU/GPU) │ │
│ └─────────────────┘ └────────────────────────────┘ │
│ │
│ ┌─────────────────────────────────────────────────────┐│
│ │ Interação com Usuário ││
│ │ • Insert → Mostrar/Ocultar ││
│ │ • Home → Sair ││
│ │ • Bandeja → Duplo clique alterna visibilidade ││
│ └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────┘

### Decisões Técnicas

| Decisão | Motivo |
|---------|--------|
| **WinMain** | Aplicação GUI sem console |
| **WS_EX_TOPMOST** | Overlay sempre visível |
| **Double-buffering** | Elimina flicker no desenho |
| **Média móvel da CPU** | Suaviza oscilações |
| **Fallbacks (WMI/Registro)** | Compatibilidade com qualquer hardware |
| **MsgWaitForMultipleObjects** | Baixo consumo de CPU |
| **Hotkeys globais** | Funcionam mesmo com overlay oculto |
| **Bandeja do sistema** | Programa roda em segundo plano |
