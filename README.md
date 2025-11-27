# mod_kernel_6.14
Roteiro para modificar Lubuntu para RTOS:

### Download Lubuntu com kernel 6.14:
https://cdimage.ubuntu.com/lubuntu/releases/noble/release/lubuntu-24.04.3-desktop-amd64.iso

### Download VirtualBox
https://www.virtualbox.org/wiki/Downloads

Instalar em máquina virtual com minimo 30GB de espaço em disco.

## Terminal:
```
mkdir ~/kernel
cd ~/kernel
wget https://mirrors.edge.kernel.org/pub/linux/kernel/v6.x/linux-6.14.tar.gz
wget https://mirrors.edge.kernel.org/pub/linux/kernel/projects/rt/6.14/patch-6.14-rt3.patch.xz
tar -xvf linux-6.14.tar.gz
xz -d patch-6.14-rt3.patch.xz
cd linux-6.14
patch -p1 <../patch-6.1.158-rt58.patch
cp /boot/config-6.14.0-27-generic .config
make oldconfig
sudo apt update && sudo apt install -y make gcc libncurses-dev libssl-dev flex libelf-dev bison libdw-dev libdwarf-dev elfutils libelf-dev gawk git
```
Antes de executar o Menuconfig, **maxime** o terminal.
```
make menuconfig
```

## Menuconfig
- Navegar em: General setup -> “Preemption Model”
- Ativar a opção: “Fully Preemptible Kernel (Real-Time)” 
- SAVE e EXIT.

# No arquivo .config :

- Limpar as keys dentro das aspas

```CONFIG_SYSTEM_TRUSTED_KEYS=""```

```CONFIG_SYSTEM_REVOCATION_KEYS=""```

## Terminal:
```
scripts/config --disable SYSTEM_REVOCATION_KEYS
```
```
make -jX (X = numero de nucleos da vm)
```
```
sudo make modules_install
sudo make install
sudo update-grub
```
```
sudo shutdown -r now
```

## Após reiniciar:  
```
uname -a
```
Você verá uma tela tela com as informações (exemplo)

```Linux <nome da distro> <versão>-rt<versão> #1 SMP PREEMPT RT <data hora GMT ano> <arquitetura> GNU/Linux```

## Algumas recomendações para compilar
- O disco tem que ser no mínimo de 20 GB (o PREEMPT_RT é maior que o kernel simples), porém 20 GB tende a sobrar menos de 2 GB para codificar ou fazer outras aplicações;
- Se fizer em um Máquina Virtual, aconselho a criar um segundo disco ligado a máquina com mais 20 GB (pelo menos) e compilar lá o kernel novo, pois assim, consegue excluir essas informações depois de atualizar o kernel
