default:
	$(MAKE) -C /lib/modules/`uname -r`/build M=$(PWD)

clean:
	$(MAKE) -C /lib/modules/`uname -r`/build M=$(PWD) clean
	find ../../src/ -name "*.o" -type f -delete

help:
	$(MAKE) -C /lib/modules/`uname -r`/build M=$(PWD) help

modules:
	$(MAKE) -C /lib/modules/`uname -r`/build M=$(PWD) modules


