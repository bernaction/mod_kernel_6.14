Roteiro para modificar Lubuntu para RTOS (Real-Time Operating System)

Este guia apresenta o passo a passo completo para compilar e instalar um kernel Linux com patch PREEMPT_RT, transformando Lubuntu em um sistema operacional de tempo real.

---

## 📋 Índice

1. [Pré-requisitos](#pré-requisitos)
2. [Atualização do Sistema](#1-atualização-do-sistema)
3. [Instalação de Dependências](#2-instalação-de-dependências)
4. [Download do Kernel e Patch RT](#3-download-do-kernel-e-patch-rt)
5. [Aplicação do Patch PREEMPT_RT](#4-aplicação-do-patch-preempt_rt)
6. [Configuração do Kernel](#5-configuração-do-kernel)
7. [Compilação do Kernel](#6-compilação-do-kernel)
8. [Instalação do Kernel](#7-instalação-do-kernel)
9. [Configuração do GRUB](#8-configuração-do-grub)
10. [Verificação da Instalação](#9-verificação-da-instalação)

---

## Pré-requisitos

- [Imagem Lubuntu]([versão 24.04.3](https://cdimage.ubuntu.com/lubuntu/releases/noble/release/lubuntu-24.04.3-desktop-amd64.iso): 
- [VirtualBox](https://www.virtualbox.org/wiki/Downloads) - Instalar em máquina virtual com minimo 30GB de espaço em disco.
- Conexão com a internet
- Pelo menos 30GB de espaço livre em disco
- Pelo menos 8GB de RAM (recomendado)

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
sudo apt install -y build-essential libncurses-dev bison flex libssl-dev libelf-dev dwarves zstd fakeroot wget curl
```
**O que faz:**
- `build-essential`: Instala compiladores (gcc, g++) e ferramentas essenciais para compilação
- `libncurses-dev`: Biblioteca para interface de menus no terminal (usada pelo menuconfig)
- `bison`: Gerador de analisadores sintáticos, necessário para compilar o kernel
- `flex`: Gerador de analisadores léxicos, trabalha junto com o bison
- `libssl-dev`: Bibliotecas de desenvolvimento SSL para assinatura de módulos
- `libelf-dev`: Bibliotecas para manipulação de arquivos ELF (formato executável do Linux)
- `dwarves`: Ferramentas para manipulação de informações de debug (inclui pahole, necessário para BTF)
- `zstd`: Algoritmo de compressão usado pelo kernel moderno
- `fakeroot`: Permite executar comandos como se fosse root sem privilégios reais (para criar pacotes)
- `wget`: Ferramenta de linha de comando para download de arquivos
- `curl`: Ferramenta para transferência de dados via URLs

---

## 3. Download do Kernel e Patch RT

```bash
cd ~
```
**O que faz:** Muda para o diretório home do usuário atual.

```bash
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

```bash
make olddefconfig
```
**O que faz:** Atualiza a configuração copiada, aplicando valores padrão para novas opções que não existiam na configuração antiga.

```bash
make menuconfig
```
**O que faz:** Abre uma interface gráfica no terminal para configurar as opções do kernel.

### Configurações importantes no menuconfig:

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

## 6. Compilação do Kernel

```bash
nproc
```
**O que faz:** Mostra o número de núcleos de processamento disponíveis no seu sistema.

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

## 7. Instalação do Kernel

```bash
sudo make modules_install
```
**O que faz:** Instala os módulos compilados em `/lib/modules/[versão-do-kernel]/`.

```bash
sudo make install
```
**O que faz:** Instala o kernel compilado em `/boot/` e atualiza automaticamente o GRUB.

---

## 8. Configuração do GRUB

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

## 9. Verificação da Instalação

```bash
sudo reboot
```
**O que faz:** Reinicia o computador para carregar o novo kernel.

Após reiniciar:

```bash
uname -r
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

## 🔧 Solução de Problemas

### Erro: "No rule to make target 'debian/canonical-certs.pem'"
```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
```
**O que faz:** Desabilita opções que requerem certificados específicos do Ubuntu/Canonical.

### Erro relacionado a BTF
```bash
scripts/config --disable DEBUG_INFO_BTF
```
**O que faz:** Desabilita a geração de informações BTF que pode causar erros em alguns sistemas.

### Voltar para o kernel original
Selecione o kernel original no menu do GRUB durante a inicialização (pressione Shift durante o boot para ver o menu).

---

## 📚 Referências

- [Kernel.org](https://kernel.org) - Código fonte oficial do kernel Linux
- [PREEMPT_RT Wiki](https://wiki.linuxfoundation.org/realtime/start) - Documentação oficial do projeto RT
- [Lubuntu](https://lubuntu.me) - Sistema operacional base
- [Felipe Viel, MSc.](https://private-zinc-3e1.notion.site/RTOS-e-T-picos-Adicionais-9326bf8826564feca96b5516ea816fa3) - Professor de Sistemas em Tempo Real

---

## 📝 Notas

- Este processo modifica componentes críticos do sistema. Faça backup antes de começar.
- Mantenha o kernel original instalado para poder reverter em caso de problemas.
- A versão do patch RT deve corresponder exatamente à versão do kernel.
- O disco tem que ser no mínimo de 20 GB (o PREEMPT_RT é maior que o kernel simples), porém 20 GB tende a sobrar menos de 2 GB para codificar ou fazer outras aplicações;
- Se fizer em um Máquina Virtual, aconselho a criar um segundo disco ligado a máquina com mais 20 GB (pelo menos) e compilar lá o kernel novo, pois assim, consegue excluir essas informações depois de atualizar o kernel

