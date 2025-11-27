# mod_kernel_6.14
Roteiro para modificar Lubuntu para RTOS:

Download Lubuntu com kernel 6.14:
https://cdimage.ubuntu.com/lubuntu/releases/noble/release/lubuntu-24.04.3-desktop-amd64.iso

Download VirtualBox

Assim instalado em máquina virtual com minimo 30GB de espaço em disco, executar estes comandos:

mkdir ~/kernel

cd ~/kernel

wget https://mirrors.edge.kernel.org/pub/linux/kernel/v6.x/linux-6.14.tar.gz

wget https://mirrors.edge.kernel.org/pub/linux/kernel/projects/rt/6.1/patch-6.1.158-rt58.patch.xz

tar -xvf linux-6.14.tar.gz

xz -d patch-6.1.158-rt58.patch.xz

cd linux-6.14

patch -p1 <../patch-6.1.158-rt58.patch

