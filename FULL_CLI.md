## Primeira parte
- atualiza o sistema
- instala todas as dependências
- baixa o kernel e o patch PREEMPT_RT
- aplica o patch
- prepara a configuração
- ativa o modo Real-Time via comandos
- compila o kernel
```bash
sudo apt update
sudo apt upgrade -y
sudo apt dist-upgrade -y
sudo apt install -y build-essential libdwarf-dev libncurses-dev libdw-dev \
  bison flex libssl-dev libelf-dev dwarves zstd elfutils fakeroot wget curl gawk
cd ~
mkdir -p kernel-rt && cd kernel-rt
wget https://mirrors.edge.kernel.org/pub/linux/kernel/v6.x/linux-6.14.tar.gz
wget https://mirrors.edge.kernel.org/pub/linux/kernel/projects/rt/6.14/patch-6.14-rt3.patch.xz
tar -xvf linux-6.14.tar.gz
xz -d patch-6.14-rt3.patch.xz
cd linux-6.14
patch -p1 <../patch-6.14-rt3.patch
cp /boot/config-$(uname -r) .config
scripts/config --disable DEBUG_INFO_BTF
scripts/config --enable DEBUG_INFO_DWARF4
make olddefconfig
scripts/config --disable PREEMPT_NONE
scripts/config --disable PREEMPT_VOLUNTARY
scripts/config --disable PREEMPT
scripts/config --enable PREEMPT_RT
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
make olddefconfig
make -j$(nproc)
```

## Segunda parte
- compila os módulos
- instala os módulos no sistema
- instala o kernel em /boot
- atualiza o GRUB
```bash
make modules -j$(nproc)
sudo make modules_install
sudo make install
sudo update-grub
```
- após confirmar êxito anterior, reinicia
```bash
sudo shutdown -r now
```

## Terceira parte
- versão do kernel
- flags PREEMPT_RT
- se o kernel realmente está em modo real-time
- mensagens do dmesg confirmando RT
```bash
uname -r
uname -v
cat /sys/kernel/realtime
dmesg | grep -i "preempt"
```
