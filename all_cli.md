```bash
sudo apt update
sudo apt upgrade -y
sudo apt dist-upgrade -y
sudo apt install -y build-essential libdwarf-dev libncurses-dev libdw-dev bison flex libssl-dev libelf-dev dwarves zstd elfutils fakeroot wget curl
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
make -j$(nproc)
```

```bash
make modules -j$(nproc)
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

```bash
uname -r
uname -v
cat /sys/kernel/realtime
dmesg | grep -i "preempt"
```
