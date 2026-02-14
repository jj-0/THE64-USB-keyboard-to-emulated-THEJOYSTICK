#!/bin/sh
# Kill the64
until [ `ps | grep -e the64 | wc -l` -eq 1 ]
do
        killall the64
done

mount -o remount,rw /mnt
cd /mnt

export THE64MODEL=$1

case $THE64MODEL in

ARGENT|SHIELD|INERTIA|argent|shield|inertia)
	insmod /mnt/uinput.ko
	if [ ! -c /dev/uinput ]
	then
		mount -t devtmpfs devtmpfs /dev
		ln -s /tmp/usbdrive /dev/usbdrive
		mkdir -p /dev/pts
		mount -t devpts devpts /dev/pts
		mkdir /dev/shm
	fi
	;;

AMORA|ARES|SNOWBIRD|amora|ares|snowbird)
	;;

*)
	;;
esac

cp /mnt/keyboard2thejoystick /tmp
/tmp/keyboard2thejoystick &

the64 &
