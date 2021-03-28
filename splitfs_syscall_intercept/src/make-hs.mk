

define \n


endef



#    nanodd    #

NDD_RUNTIME = 80

NDD_TARGET_DIR  = /tmp/memuram0
NDD_TARGET_FILE = xddtestfile.txt
NDD_OUT_DIR = ~/results

NDDEXEC  = $(BEE3HOME)/Workloads/xdd/bin/xdd.linux
NDDFLAGS = -noproclock -nomemlock -reqsize 1 -dio -seek random -id $(NDD_LOG_FILENAME) -runtime $(NDD_RUNTIME)

NDD_WRAPPER_DIR = $(BEE3HOME)/Tools/PosixNVM/bin
NDD_WRAPPER_SCRIPTS = nvpUnmod nvpNVP
NDD_RATIOS = 0 25 50 75 100
NDD_NUM_THREADS = 1 4 16 64
NDD_FILE_LENS_KB = 57671680
NDD_REQ_SIZE = 512 4096 131072
NDD_LOG_FILENAME = $(SCRIPT)-tc$(THREADS)-rw$(RATIO)-filelen$(FILELEN)k-reqsize$(REQSIZE).result

NDD_AFFINITY_1 = taskset -c 0,2,4,6
NDD_AFFINITY_2 = taskset -c 0,2,4,6
NDD_AFFINITY_4 = taskset -c 0,2,4,6
NDD_AFFINITY_8 = taskset -c 0,2,4,6,8,10,12,14
NDD_AFFINITY_16= taskset -c 0-15
NDD_AFFINITY_32= taskset -c 0-15
NDD_AFFINITY_64= taskset -c 0-15
NDD_AFFINITY_12= taskset -c 0-15
NDD_AFFINITY_24= taskset -c 0-15

SHELL = /bin/bash

NDD_G1_DIR=2012-02-13-01
NDD_G2_DIR=09

test_prep_ramdisk:
	-sudo umount /tmp/memuram0
	-sudo rmmod memudisk
	sudo insmod $(BEE3HOME)/Tools/KernelModules/Ramdisk/memudisk.ko rd_size=$$[64*1024*1024] max_part=1 rd_nr=1
	sleep 2
	sudo chmod 777 /dev/memuram0
	sudo mke2fs /dev/memuram0 -b 4096
	sudo mount /dev/memuram0 /tmp/memuram0 -o xip
	sudo chmod 777 /tmp/memuram0
	-dd if=/dev/zero of=$(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) bs=$$[4096*16] count=$$[59055800320/(4096*16)] oflag=direct
	sleep 10

test_prep_ramdisk_3:
	-sudo umount /tmp/memuram0
	-sudo rmmod brd
	sudo insmod /lib/modules/3.2.1-io/kernel/drivers/block/brd.ko rd_size=$$[64*1024*1024] max_part=1 rd_nr=1
	sleep 2
	sudo chmod 777 /dev/ram0
	sudo mke2fs /dev/ram0 -b 4096
	sudo mount /dev/ram0 /tmp/memuram0 -o xip
	sudo chmod 777 /tmp/memuram0
	-dd if=/dev/zero of=$(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) bs=$$[4096*16] count=$$[59055800320/(4096*16)] oflag=direct
	sleep 10

test_prep_ramdisk_hugepage:
	-sudo umount /tmp/memuram0
	-sudo umount /tmp/memuram0
	#-sudo rmmod brd
	#sudo insmod /lib/modules/3.2.1-io/kernel/drivers/block/brd.ko rd_size=$$[64*1024*1024] max_part=1 rd_nr=1
	#sleep 2
	#sudo chmod 777 /dev/ram0
	#sudo mke2fs /dev/ram0 -b 4096
	#sudo mount /dev/ram0 /tmp/memuram0 -o xip
	#sudo chmod 777 /tmp/memuram0
	#-dd if=/dev/zero of=$(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) bs=$$[2*1024*1024] count=$$[59055800320/(2*1024*1024/4)] oflag=direct
	#sleep 2
	sudo mount -t hugetlbfs -o rw,pagesize=2M,mode=0777 none /tmp/memuram0
	sudo chmod 777 /tmp/memuram0
	sleep 2

break_ramdisk:
	/homes/leisner/bee3/Tools/PosixNVM/bin/nvpNVP /homes/leisner/bee3/Workloads/fastdd/fastdd.exe asdf -footKB 57671680 -r 0 -s 512 -tc 32 -file /tmp/memuram0/xddtestfile.txt -rt 60

HSG1_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) $(NDDEXEC) $(NDDFLAGS) -queuedepth $(THREADS) -kbytes $(FILELEN) -rwratio $(RATIO) -blocksize $(REQSIZE) -seek range $$[($(FILELEN)*1024)/$(REQSIZE)] -target $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE)

HSG1_MEGADD_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) ./megadd $(NDD_RUNTIME) $(FILELEN) $(REQSIZE) $(RATIO)

HSG1_FASTDD_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) ${BEE3HOME}/Workloads/fastdd/fastdd.exe asdf -footKB $(FILELEN) -r $(RATIO) -s $(REQSIZE) -tc $(THREADS) -file $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) -rt $(NDD_RUNTIME)

test_xdd_hsg1:
	#megadd test_prep_ramdisk
	#$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	$(foreach THREADS, $(NDD_NUM_THREADS), \
	$(foreach RATIO,   $(NDD_RATIOS), \
	$(foreach FILELEN, $(NDD_FILE_LENS_KB), \
	$(foreach REQSIZE, $(NDD_REQ_SIZE), \
	$(foreach SCRIPT,  $(NDD_WRAPPER_SCRIPTS), \
	echo -n "Running test $@ on host " > $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME); echo -n "in directory " >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME); pwd >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME); ${\n}\
	echo "$(HSG1_FASTDD_CMD)" >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME) ; ${\n} \
	2>&1 $(HSG1_FASTDD_CMD) >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/FS-$(NDD_LOG_FILENAME); sleep 10 ${\n} \
	)))))


HSG2_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) $(NDDEXEC) $(NDDFLAGS) -queuedepth $(THREADS) -kbytes $(FILELEN) -rwratio $(RATIO) -blocksize $(REQSIZE) -seek range $$[($(FILELEN)*1024)/$(REQSIZE)] -target $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) -startoffset 4096

HSG2_MEGADD_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) ./megadd $(NDD_RUNTIME) $(FILELEN) $(REQSIZE) $(RATIO)

HSG2_FASTDD_CMD=$(NDD_AFFINITY_$(THREADS)) $(NDD_WRAPPER_DIR)/$(SCRIPT) ${BEE3HOME}/Workloads/fastdd/fastdd.exe asdf -footKB $(FILELEN) -r $(RATIO) -s $(REQSIZE) -tc $(THREADS) -file $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) -rt $(NDD_RUNTIME)


test_xdd_hsg2: test_prep_ramdisk
	#$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	$(foreach THREADS, $(NDD_NUM_THREADS), \
	$(foreach RATIO,   $(NDD_RATIOS), \
	$(foreach FILELEN, $(NDD_FILE_LENS_KB), \
	$(foreach REQSIZE, 4096, \
	$(foreach SCRIPT,  $(NDD_WRAPPER_SCRIPTS), \
	echo -n "Running test $@ on host " > $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); echo -n "in directory " >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); pwd >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); ${\n}\
	echo "$(HSG2_FASTDD_CMD)" 2>&1 >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); ${\n}\
	$(HSG2_FASTDD_CMD) 2>&1 >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); sleep 1 ${\n} \
	)))))

establish_mem_baseline:
	-sudo rmmod memudisk.ko
	/homes/leisner/bee3/Workloads/fastmm/fastmm.exe asdf -footKB 57671680 -r 100 -s 4096 -tc 16 -file /dev/memuram5 -rt 90
	/homes/leisner/bee3/Workloads/fastmm/fastmm.exe asdf -footKB 57671680 -r 0   -s 4096 -tc 16 -file /dev/memuram5 -rt 90
	sudo insmod ~leisner/bee3/Tools/KernelModules/Ramdisk/memudisk.ko && sleep 2
	sudo chmod 777 /dev/memuram5
	/homes/leisner/bee3/Workloads/fastdd/fastdd.exe asdf -footKB 57671680 -r 100 -s 4096 -tc 16 -file /dev/memuram5 -rt 90
	sudo rmmod memudisk.ko
	sudo insmod ~leisner/bee3/Tools/KernelModules/Ramdisk/memudisk.ko && sleep 2
	sudo chmod 777 /dev/memuram5
	/homes/leisner/bee3/Workloads/fastdd/fastdd.exe asdf -footKB 57671680 -r 0   -s 4096 -tc 16 -file /dev/memuram5 -rt 90
	sudo rmmod memudisk.ko

sweep_cores:
	$(foreach CORE, 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15, taskset -c $(CORE) /homes/leisner/bee3/Workloads/fastmm/fastmm.exe asdf -footKB 4194304 -r 100 -s 4096 -tc 1 -file /dev/foochar -rt 10 2>&1 | grep asdf; )
	#$(foreach CORE, 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15, taskset -c $(CORE) /homes/leisner/bee3/Workloads/fastdd/fastdd.exe asdf -footKB 57671680 -r 100 -s 4096 -tc 8 -file /mnt/foodisk/zero.txt -rt 10 2>&1 | grep asdf; )

FOO_TARGET_DIR  = /mnt/foodisk
FOO_TARGET_FILE = zero.txt
#FOO_TARGET_DIR  = /tmp/memuram0
#FOO_TARGET_FILE = xddtestfile.txt


#	$(foreach THREADS, 1 2 4 8 12 16 24 32, \
#	$(foreach RATIO,   0 25 50 75 100, \
#	$(foreach FILELEN, 16 256 4096 65536 1048576 16777216 57671680, \
#	$(foreach REQSIZE, 8 128 512 4096 16384 65536 262144 1048576 4194304 16777216 511 513 4191 4193, \

#	rm $(FOO_TARGET_DIR)/$(FOO_TARGET_FILE) ; \
#	dd if=/dev/zero of=$(FOO_TARGET_DIR)/$(FOO_TARGET_FILE) seek=0 count=1 bs=1 ; \

FOO_OUT_DIR = ~/results
FOO_G1_DIR=2012-06-07-02
FOO_LOG_FILENAME = $(SCRIPT)-tc$(THREADS)-rw$(RATIO)-filelen$(FILELEN)k-reqsize$(REQSIZE).result

FOO_WRAPPER_DIR = $(BEE3HOME)/Tools/PosixNVM/bin
FOOEXEC  = $(BEE3HOME)/Workloads/xdd/bin/xdd.linux

FOO_RUNTIME = 40

FOO_G1_CMD=$(NDD_AFFINITY_$(THREADS)) $(FOO_WRAPPER_DIR)/$(SCRIPT) ${BEE3HOME}/Workloads/fastdd/fastdd.exe asdf -footKB $(FILELEN) -r $(RATIO) -s $(REQSIZE) -tc $(THREADS) -file $(FOO_TARGET_DIR)/$(FOO_TARGET_FILE) -rt $(FOO_RUNTIME)

test_foodisk_1:
	mkdir $(FOO_OUT_DIR)/$(FOO_G1_DIR)
	#$(MAKE) -C foomodule all prepd
	@echo `date` ": Start of test $@" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt
	$(foreach THREADS, 1 2 4 8 12 16, \
	$(foreach RATIO,   0 50 100, \
	$(foreach FILELEN, 256 4096 65536 1048576 16777216 57671680, \
	$(foreach REQSIZE, 8 512 4096 16384, \
	$(foreach SCRIPT,  nvpUnmod nvpNVP, \
	echo -n "Running test $@ on host " > $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME); echo -n "in directory " >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME); pwd >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME); ${\n}\
	echo "$(FOO_G1_CMD)" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME) ; ${\n} \
	@echo `date` ": Start of test " $(FOO_LOG_FILENAME) >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt ; ${\n} \
	2>&1 $(FOO_G1_CMD) >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_LOG_FILENAME); sleep 10 ${\n} \
	)))))
	@echo `date` ": End of test $@" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt

FOO_G2_CMD=$(NDD_AFFINITY_$(THREADS)) $(FOO_WRAPPER_DIR)/$(SCRIPT) ${BEE3HOME}/Workloads/fastmeta/fastmeta.exe asdf -footKB $(FILELEN) -r $(RATIO) -s $(REQSIZE) -tc $(THREADS) -file $(FOO_TARGET_DIR)/meta -F $(FILECOUNT) -rt $(FOO_RUNTIME)
FOO_G2_LOG_FILENAME = $(SCRIPT)-tc$(THREADS)-rw$(RATIO)-filelen$(FILELEN)k-reqsize$(REQSIZE)-filecount$(FILECOUNT).result

test_foodisk_2:
	mkdir $(FOO_OUT_DIR)/$(FOO_G1_DIR)
	#$(MAKE) -C foomodule all prepd
	@echo `date` ": Start of test $@" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt
	$(foreach THREADS, 1 4 12, \
	$(foreach RATIO,  0 50, \
	$(foreach FILELEN, 57671680, \
	$(foreach FILECOUNT, 1 4 16 64, \
	$(foreach REQSIZE, 8 512 4192 65536 1048576 16777216, \
	$(foreach SCRIPT,  nvpUnmod nvpNVP, \
	rm -rf $(FOO_TARGET_DIR)/meta ; mkdir $(FOO_TARGET_DIR)/meta ; \
	echo -n "Running test $@ on host " > $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME); echo -n "in directory " >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME); pwd >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME); ${\n}\
	echo "$(FOO_G2_CMD)" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME) ; ${\n} \
	@echo `date` ": Start of test " $(FOO_G2_LOG_FILENAME) >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt ; ${\n} \
	2>&1 $(FOO_G2_CMD) >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/FS-$(FOO_G2_LOG_FILENAME); sleep 10 ${\n} \
	))))))
	@echo `date` ": End of test $@" >> $(FOO_OUT_DIR)/$(FOO_G1_DIR)/log.txt


#test_compare_dd: test_prep_ramdisk
#	$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
#	$(MAKE) -C $(BEE3HOME)/Workloads/fastdd clean all
#	$(CC) megadd.c -o megadd $(CFLAGS)
#	$(BEE3HOME)/Workloads/fastdd/fastdd.exe asdf -rt 600 -file /tmp/memuram0/xddtestfile.txt -foot $[57671680/1024/1024] -r 100 -s 4096 2&>1 > /x/HotStorage2011-NVP/logs/comparedd/fastdd.result
#	$(BEE3HOME)/Workloads/xdd/bin/xdd.linux -noproclock -nomemlock -reqsize 1 -dio -seek random -target /tmp/memuram0/xddtestfile.txt -runtime 600 -queuedepth 1 -kbytes 57671680 -rwratio 100 -blocksize 4096 -seek range $[57671680/4096] 2&>1 > /x/HotStorage2011-NVP/logs/comparedd/xdd.result
#	./megadd 2&>1 > /x/HotStorage2011-NVP/logs/comparedd/megadd.result


BDB_TARGET_DIR  = /tmp/memuram0

BDB_WRAPPER_DIR = $(BEE3HOME)/Tools/PosixNVM/bin
BDB_WRAPPER_SCRIPTS = nvpNVP nvpUnmod
BDB_NUM_THREADS = 1 2 4 8 16
BDB_LOG_FILENAME = BDB-$(STRUCT)-$(SCRIPT)-tc$(THREADS).result

BDB_TREE_EXEC = $(BEE3HOME)/Workloads/BDB/Multi/BTree.exe
BDB_HASH_EXEC = $(BEE3HOME)/Workloads/BDB/Multi/HashTable.exe

BDB_STORAGE_STRUCTS = TREE HASH

BDB_FLAGS = -footMB 16384 -reload -file $(BDB_TARGET_DIR)/$(BDB_$(STRUCT)_DIR) -F -rt 600

BDB_TREE_DIR = BTree
BDB_HASH_DIR = HashTable

BDB_OUT_DIR = /x/FAST2012-NVP/data/bdb/01

BDB_CMD = $(BDB_WRAPPER_DIR)/$(SCRIPT) $(BDB_$(STRUCT)_EXEC) bdb-$(STRUCT)-$(SCRIPT) $(BDB_FLAGS) -tc $(THREADS)

test_bdb_hsg3: test_prep_ramdisk
	#$(MAKE) -C$(BEE3HOME)/Workloads/BDB/Multi all
	-rm $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE)
	$(foreach SCRIPT,  $(BDB_WRAPPER_SCRIPTS), \
	$(foreach THREADS, $(BDB_NUM_THREADS), \
	$(foreach STRUCT,  $(BDB_STORAGE_STRUCTS), \
	rm -rf $(BDB_TARGET_DIR)/$(BDB_$(STRUCT)_DIR) ; \
	cp -r /x/SC2010/benchmark_inputs/bdb/$(BDB_$(STRUCT)_DIR) $(BDB_TARGET_DIR)/$(BDB_$(STRUCT)_DIR) ${\n} \
	echo -n "Running test $@ on host " > $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME); echo -n "in directory " >> $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME); pwd >> $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME); ${\n}\
	echo "$(BDB_CMD)" >> $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME) ; ${\n} \
	$(BDB_CMD) 2&>1 >> $(BDB_OUT_DIR)/$(BDB_LOG_FILENAME) ; \
	sleep 1 ${\n} \
	)))


HSG2_ALT_CMD=$(NDD_AFFINITY) $(NDD_WRAPPER_DIR)/$(SCRIPT) $(NDDEXEC) $(NDDFLAGS) -queuedepth $(THREADS) -kbytes $(FILELEN) -rwratio $(RATIO) -blocksize $(REQSIZE) -seek range $$[($(FILELEN)*1024)/$(REQSIZE)] -target $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) 2>&1 >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME)

test_xdd_hsg2_alt:
	#$(MAKE) -C $(BEE3HOME)/Workloads/xdd all
	$(foreach THREADS, 1, \
	$(foreach RATIO,   $(NDD_RATIOS), \
	$(foreach FILELEN, $(NDD_FILE_LENS_KB), \
	$(foreach REQSIZE, 4096, \
	$(foreach SCRIPT,  $(NDD_WRAPPER_SCRIPTS), \
	-sudo umount /tmp/memuram0; ${\n} \
	-sudo rmmod memudisk; ${\n} \
	sudo insmod $(BEE3HOME)/Tools/KernelModules/Ramdisk/memudisk.ko rd_size=$$[64*1024*1024] max_part=1 rd_nr=1 ; \
	sleep 2 ; ${\n} \
	sudo chmod 777 /dev/memuram0; ${\n} \
	sudo mke2fs /dev/memuram0 -b 4096 ; ${\n} \
	sudo mount /dev/memuram0 /tmp/memuram0 -o xip ; ${\n} \
	sudo chmod 777 /tmp/memuram0 ; ${\n} \
	dd if=/dev/zero of=$(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) bs=$$[1024*1024] count=$$[57671680/1024] oflag=direct; ${\n} \
	rm $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE); ${\n} \
	-dd if=/dev/zero of=$(NDD_TARGET_DIR)/$(NDD_TARGET_FILE) bs=4096 count=$$[$(FILELEN)/4] oflag=direct ; \
	sleep 2 ; ${\n} \
	echo -n "Running test $@ on host " > $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); echo -n "in directory " >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); pwd >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); ${\n}\
	echo "$(HSG2_ALT_CMD)" >> $(NDD_OUT_DIR)/$(NDD_G2_DIR)/$(NDD_LOG_FILENAME); ${\n} \
	$(HSG2_ALT_CMD); sleep 10 ${\n} \
	)))))

test_xdd_hsg1_dev: test_prep_ramdisk
	sudo umount /tmp/memuram0
	$(foreach THREADS, $(NDD_NUM_THREADS), \
	$(foreach RATIO,   $(NDD_RATIOS), \
	$(foreach FILELEN, 57671680, \
	$(foreach REQSIZE, $(NDD_REQ_SIZE), \
	$(foreach SCRIPT,  $(NDD_WRAPPER_SCRIPTS), \
	echo -n "Running test $@ on host " > $(NDD_OUT_DIR)/$(NDD_G1_DIR)/dev-$(NDD_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/dev-$(NDD_LOG_FILENAME); echo -n "in directory " >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/dev-$(NDD_LOG_FILENAME); pwd >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/dev-$(NDD_LOG_FILENAME); ${\n}\
	$(NDD_AFFINITY) $(NDD_WRAPPER_DIR)/$(SCRIPT) $(NDDEXEC) $(NDDFLAGS) -queuedepth $(THREADS) -kbytes $(FILELEN) -rwratio $(RATIO) -blocksize $(REQSIZE) -seek range $$[($(FILELEN)*1024)/$(REQSIZE)] -target /dev/memuram0 2>&1 >> $(NDD_OUT_DIR)/$(NDD_G1_DIR)/dev-$(NDD_LOG_FILENAME); sleep 1 ${\n} \
	)))))
	
#grep -H "Combined" $(NDD_OUT_DIR)/*.result > $(NDD_OUT_DIR)/summary.result



OLTP_TARGET_DIR  = /mnt/foodisk

OLTP_WRAPPER_DIR = $(BEE3HOME)/Tools/PosixNVM/bin
OLTP_WRAPPER_SCRIPTS = nvpNVP nvpUnmod
OLTP_NUM_THREADS = 1 16
OLTP_LOG_FILENAME = OLTP-$(SCRIPT)-tc$(THREADS).result

OLTP_DEFAULTS=${BEE3HOME}/Tools/PosixNVM/oltp-config-foo.cnf

OLTP_TIME=60

OLTP_OUT_DIR = ~/results/2012-05-01-01

#OLTP_REDIRECT= 2&>1 >> $(OLTP_OUT_DIR)/$(OLTP_LOG_FILENAME) 
OLTP_REDIRECT = 

OLTP_CMD = $(OLTP_WRAPPER_DIR)/$(SCRIPT) $(OLTP_$(STRUCT)_EXEC) bdb-$(STRUCT)-$(SCRIPT) $(OLTP_FLAGS) -tc $(THREADS)

#test_oltp_hsg3: test_prep_ramdisk
test_oltp_hsg3: 
	rm -f $(NDD_TARGET_DIR)/$(NDD_TARGET_FILE)
	$(foreach SCRIPT,  $(OLTP_WRAPPER_SCRIPTS), \
	$(foreach THREADS, $(OLTP_NUM_THREADS), \
	echo -n "Running test $@ on host " > $(OLTP_OUT_DIR)/$(OLTP_LOG_FILENAME); hostname|awk '{ printf "%s ", $$0 }' >> $(OLTP_OUT_DIR)/$(OLTP_LOG_FILENAME); echo -n "in directory " >> $(OLTP_OUT_DIR)/$(OLTP_LOG_FILENAME); pwd >> $(OLTP_OUT_DIR)/$(OLTP_LOG_FILENAME); ${\n}\
	rm -rf $(OLTP_TARGET_DIR)/mysql; mkdir $(OLTP_TARGET_DIR)/mysql; sleep 1; ${\n} \
	mysqld_safe --defaults-file=$(OLTP_DEFAULTS); sleep 1; ${\n} \
	mysql_install_db --basedir=${BEE3HOME}/ext/mysql-5.1.46/install --datadir=$(OLTP_TARGET_DIR)/mysql --defaults-file=$(OLTP_DEFAULTS) $(OLTP_REDIRECT) ; ${\n}\
	cd ${BEE3HOME}/ext/mysql-5.1.46/install; ./bin/mysqld_safe_moneta --defaults-file=$(OLTP_DEFAULTS) --nvp-preloads='$(OLTP_WRAPPER_DIR)/$(SCRIPT)' $(OLTP_REDIRECT) & ${\n}\
	sleep 180; ${\n}\
	mysql -u root < ${BEE3HOME}/Automate/SC2010/scripts/util/oltp_init.sql $(OLTP_REDIRECT); ${\n}\
	$(BEE3HOME)/ext/sysbench-0.4.12/sysbench/sysbench --test=oltp --db-driver=mysql --mysql-table-engine=innodb --mysql-user=root --mysql-password= --mysql-socket=/tmp/mysql.sock --oltp-table-size=32000000 prepare $(OLTP_REDIRECT) ; ${\n}\
	sleep 80 ; ${\n}\
	$(BEE3HOME)/ext/sysbench-0.4.12/install/bin/sysbench --num-threads=$(THREADS) --max-time=$(OLTP_TIME) --max-requests=0 --test=oltp --oltp-table-size=32000000 --db-driver=mysql --mysql-table-engine=innodb --mysql-user=root --mysql-password= --mysql-socket=/tmp/mysql.sock run $(OLTP_REDIRECT) ; ${\n}\
	sleep 10 ; \
	mysqladmin --defaults-file=/tmp/my.cnf -u root shutdown $(OLTP_REDIRECT) ; \
	sleep 10 ; ${\n}\
	))

