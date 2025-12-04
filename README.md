# 🧩 Roteiro para transformar Lubuntu em um RTOS (Real-Time Operating System)

Este guia apresenta o passo a passo completo para compilar e instalar um kernel Linux com o patch PREEMPT_RT, transformando o Lubuntu em um sistema operacional de tempo real.

## 📚 Documentação do Projeto

- **[QUICKSTART.md](QUICKSTART.md)** → 🚀 Guia rápido de uso (comece aqui!)
- **[FULL_CLI.md](FULL_CLI.md)** → 📋 Comandos consolidados (copiar/colar)
- **[TEST.md](TEST.md)** → 🧪 Testes de validação RT (cyclictest, ptsematest)
- **[RESULTADOS.md](RESULTADOS.md)** → 📊 Template para seus resultados
- **[src/README_LINUX.md](src/README_LINUX.md)** → 📖 Documentação técnica detalhada
- **[src/ADAPTACAO.md](src/ADAPTACAO.md)** → 🔄 Detalhes da adaptação ESP32→Linux

## 💽 Requisitos mínimos de hardware (VM VirtualBox)

Para evitar erros de build (falta de RAM, falta de espaço ou kernel sendo "Killed"):
- [Imagem Lubuntu](https://cdimage.ubuntu.com/lubuntu/releases/noble/release/lubuntu-24.04.3-desktop-amd64.iso) - versão 24.04.3 LTS
- [VirtualBox](https://www.virtualbox.org/wiki/Downloads) - Instalar em máquina virtual com minimo 60GB de espaço em disco.

| Recurso | Recomendado | Mínimo absoluto |
|--------|-------------|-----------------|
| **RAM** | 8 GB | 4 GB |
| **Disco** | **80 GB** | 60 GB |
| **CPUs** | 4 | 2 |

❗ **Por que 60 GB?**  
Compilar kernel RT 6.x pode usar facilmente **12–20 GB** só no diretório de build.  
Com 60GB, você **não precisa adicionar segundo disco, montar partições ou mexer em permissões.**

## 📌 Recomendações antes de iniciar

- Instale Lubuntu 24.04.3 LTS (ISO oficial)  
- Ative 3D acceleration e I/O APIC no VirtualBox  
- Use sistema de arquivos ext4  
- Evite snapshots durante a compilação (crescem muito rápido)

---


## 1. Atualização do Sistema

```bash
sudo apt update
```
**O que faz:** Atualiza a lista de pacotes disponíveis nos repositórios configurados. Não instala nada, apenas baixa as informações mais recentes sobre os pacotes disponíveis.

```bash
sudo apt upgrade -y
```
**O que faz:** Instala as atualizações disponíveis para todos os pacotes já instalados no sistema. O `-y` confirma automaticamente a instalação.

```bash
sudo apt dist-upgrade -y
```
**O que faz:** Realiza uma atualização mais inteligente que pode adicionar ou remover pacotes conforme necessário para atualizar todo o sistema.

---

## 2. Instalação de Dependências

```bash
sudo apt install -y build-essential libdwarf-dev libncurses-dev libdw-dev libnuma-dev \
  bison flex libssl-dev libelf-dev dwarves zstd elfutils fakeroot wget curl gawk
```
**O que faz:**
- `build-essential`: Instala compiladores (gcc, g++) e ferramentas essenciais para compilação
- `libdwarf-dev`: Fornece o header dwarf.h, necessário para scripts do kernel que lidam com informações de debug.
- `libncurses-dev`: Biblioteca para interface de menus no terminal (usada pelo menuconfig)
- `libdw-dev`: Bibliotecas para ler e manipular informações DWARF usadas pelo kernel (depende de elfutils).
- `libnuma-dev`: Bibliotecas para arquiteturas NUMA (Non-Uniform Memory Access), necessárias para compilar kernels otimizados para sistemas multiprocessador
- `bison`: Gerador de analisadores sintáticos, necessário para compilar o kernel
- `flex`: Gerador de analisadores léxicos, trabalha junto com o bison
- `libssl-dev`: Bibliotecas de desenvolvimento SSL para assinatura de módulos
- `libelf-dev`: Bibliotecas para manipulação de arquivos ELF (formato executável do Linux)
- `dwarves`: Ferramentas para manipulação de informações de debug (inclui pahole, necessário para BTF)
- `zstd`: Algoritmo de compressão usado pelo kernel moderno
- `elfutils`: Ferramentas para leitura/manipulação de ELF e DWARF (objdump, readelf modernos).
- `fakeroot`: Permite executar comandos como se fosse root sem privilégios reais (para criar pacotes)
- `wget`: Ferramenta de linha de comando para download de arquivos
- `curl`: Ferramenta para transferência de dados via URLs
- `gawk`: Implementação GNU da linguagem AWK, uma linguagem clássica de processamento de texto muito usada no Unix.

---

## 3. Download do Kernel e Patch RT

```bash
cd ~
mkdir -p kernel-rt && cd kernel-rt
```
**O que faz:** Cria um diretório chamado `kernel-rt` (se não existir) e entra nele. O `-p` evita erro se o diretório já existir.

```bash
wget https://mirrors.edge.kernel.org/pub/linux/kernel/v6.x/linux-6.14.tar.gz
```
**O que faz:** Baixa o código fonte do kernel Linux versão 6.14 do site oficial kernel.org.

```bash
wget https://mirrors.edge.kernel.org/pub/linux/kernel/projects/rt/6.14/patch-6.14-rt3.patch.xz
```
**O que faz:** Baixa o patch PREEMPT_RT correspondente à versão do kernel. Este patch transforma o kernel em tempo real.

---

## 4. Aplicação do Patch PREEMPT_RT

```bash
tar -xvf linux-6.14.tar.gz
```
**O que faz:** Extrai o arquivo compactado do kernel.
- `x`: Extrai arquivos
- `v`: Modo verbose (mostra arquivos sendo extraídos)
- `f`: Especifica o arquivo a ser extraído

  
```bash
xz -d patch-6.14-rt3.patch.xz
```
**O que faz:** Extrai o arquivo de patch.
- `-d`: Remove o arquivo compactado após a extração.

```bash
cd linux-6.14
```
**O que faz:** Entra no diretório do código fonte do kernel extraído.

```bash
patch -p1 <../patch-6.14-rt3.patch
```
**O que faz:**
- `patch -p1`: Aplica o patch ao código fonte. O `-p1` remove o primeiro nível de diretório dos caminhos no patch

---

## 5. Configuração do Kernel

```bash
cp /boot/config-$(uname -r) .config
```
**O que faz:** Copia a configuração do kernel atual como base para a nova compilação. `$(uname -r)` retorna a versão do kernel em execução.

## 6. Desativar BTF
```bash
scripts/config --disable DEBUG_INFO_BTF
scripts/config --enable DEBUG_INFO_DWARF4
```
**O que faz:** O kernel RT não precisa de BTF (BPF Type Format).
O kernel Linux moderno tenta gerar automaticamente uma seção chamada BTF (BPF Type Format).

Essa seção é usada pelo eBPF, ferramentas de observabilidade como bpftrace e CO-RE, além de módulos avançados de tracing.

Durante o build do kernel, o BTF é gerado pelo programa pahole (do pacote dwarves).

```bash
make olddefconfig
```
**O que faz:** Atualiza a configuração copiada, aplicando valores padrão para novas opções que não existiam na configuração antiga.

```bash
make menuconfig
```
**O que faz:** Abre uma interface gráfica no terminal para configurar as opções do kernel.

### ⚠️ Configurações importantes no menuconfig:

Navegue até: `General setup` → `Preemption Model`

Selecione: `Fully Preemptible Kernel (Real-Time)`

**Use as setas para navegar, Enter para selecionar, e Esc duas vezes para voltar.**

Após configurar, salve e saia (selecione `Save` e depois `Exit`).

```bash
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
```
**O que faz:** Desabilita a verificação de chaves confiáveis do sistema para evitar erros de compilação.

```bash
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
```
**O que faz:** Desabilita a lista de revogação de chaves para evitar erros de compilação.

---

## 7. Compilação do Kernel

```bash
make -j$(nproc)
```
**O que faz:** Compila o kernel usando todos os núcleos do processador.
- `make`: Inicia a compilação
- `-j$(nproc)`: Executa N tarefas em paralelo, onde N é o número de núcleos

> **⚠️ Atenção:** Este processo pode levar de 30 minutos a várias horas dependendo do seu hardware.

```bash
make modules -j$(nproc)
```
**O que faz:** Compila os módulos do kernel (drivers e funcionalidades carregáveis).

---

## 8. Instalação do Kernel

```bash
sudo make modules_install
```
**O que faz:** Instala os módulos compilados em `/lib/modules/[versão-do-kernel]/`.

```bash
sudo make install
```
**O que faz:** Instala o kernel compilado em `/boot/` e atualiza automaticamente o GRUB.

---

## 9. Configuração do GRUB

```bash
sudo update-grub
```
**O que faz:** Regenera o arquivo de configuração do GRUB (`/boot/grub/grub.cfg`) para incluir o novo kernel.

```bash
sudo nano /etc/default/grub
```
**O que faz:** Abre o arquivo de configuração do GRUB para edição.

### Alterações opcionais no arquivo:

Para ver o menu do GRUB na inicialização, altere:
```
GRUB_TIMEOUT_STYLE=menu
GRUB_TIMEOUT=10
```
**O que faz:** Configura o GRUB para mostrar o menu por 10 segundos antes de iniciar automaticamente.

Após editar, salve (Ctrl+O, Enter) e saia (Ctrl+X).

```bash
sudo update-grub
```
**O que faz:** Aplica as alterações, caso feitas, no arquivo de configuração do GRUB.

---

## 10. Verificação da Instalação

```bash
sudo reboot
```
**O que faz:** Reinicia o computador para carregar o novo kernel.

Após reiniciar:

```bash
sudo uname -r
```
**O que faz:** Exibe a versão do kernel em execução. Deve mostrar algo como `6.14.0-rt7-rt-custom`.

```bash
uname -v
```
**O que faz:** Exibe informações de versão do kernel, incluindo `PREEMPT_RT` se o patch foi aplicado corretamente.

```bash
cat /sys/kernel/realtime
```
**O que faz:** Verifica se o kernel é RT. Deve retornar `1` para kernel de tempo real.

```bash
dmesg | grep -i "preempt"
```
**O que faz:** Procura mensagens do kernel relacionadas a preemption. Deve mostrar informações sobre PREEMPT_RT.

---

## 🏭 Código Adaptado - Esteira Industrial RT

Após instalar o kernel PREEMPT_RT, você pode executar os programas de tempo real adaptados:

### Compilação

```bash
cd src
make
```

Isso compila:
- `esteira_linux` → Simulação da esteira industrial com 4 tarefas RT
- `servidor_periodico` → Servidor periódico para tarefas aperiódicas

### Execução

```bash
# Esteira industrial
sudo ./esteira_linux

# Servidor periódico (Ts=10ms, Cs=5ms, prio=70, 60s)
sudo ./servidor_periodico 10 5 70 60
```

### Documentação Completa

- **[src/README_LINUX.md](src/README_LINUX.md)** → Arquitetura e uso detalhado
- **[src/ADAPTACAO.md](src/ADAPTACAO.md)** → ESP32 vs Linux, APIs, métricas
- **[QUICKSTART.md](QUICKSTART.md)** → Guia rápido de execução

---

## 📚 Referências

- [Felipe Viel, MSc.](https://private-zinc-3e1.notion.site/RTOS-e-T-picos-Adicionais-9326bf8826564feca96b5516ea816fa3) - Professor de Sistemas em Tempo Real
- [Kernel.org](https://kernel.org) - Código fonte oficial do kernel Linux
- [PREEMPT_RT Wiki](https://wiki.linuxfoundation.org/realtime/start) - Documentação oficial do projeto RT
- [Lubuntu](https://lubuntu.me) - Sistema operacional base
