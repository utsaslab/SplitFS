echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
sudo rm -rf /mnt/pmem_emul/*
sudo umount /mnt/pmem_emul
sudo rmmod nova
#insmod nova.ko
#sudo modprobe nova measure_timing=1 inplace_data_updates=1
sudo modprobe nova inplace_data_updates=1
sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem_emul
