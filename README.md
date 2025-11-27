# mod_kernel_6.14
Roteiro para modificar Lubuntu para RTOS:

Download Lubuntu com kernel 6.14:
https://cdimage.ubuntu.com/lubuntu/releases/noble/release/lubuntu-24.04.3-desktop-amd64.iso

Download VirtualBox

Assim instalado em máquina virtual com minimo 30GB de espaço em disco, executar estes comandos:

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

make menuconfig

# Ativar a opção “Fully Preemptible Kernel (Real-Time)” em “General setup” / “Preemption Model”. 

# Após salvar em SAVE e sair em EXIT.

#No arquivo .config :

# Limpar as keys dentro das aspas

CONFIG_SYSTEM_TRUSTED_KEYS=""

CONFIG_SYSTEM_REVOCATION_KEYS=""

scripts/config --disable SYSTEM_REVOCATION_KEYS

make -jX (X = numero de nucleos da vm)

sudo make modules_install

sudo make install

sudo update-grub

sudo reboot



