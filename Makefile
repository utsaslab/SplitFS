all:
	@echo Nothing here

test: 
	@echo "============================ Executing Posix Mode ============================"
	$(MAKE) -C tests pjd.posix
	@echo "============================ Finished Posix Mode ============================="
	@echo "============================ Executing Sync Mode ============================="
	$(MAKE) -C tests pjd.sync
	@echo "============================ Finished Sync Mode =============================="
	@echo "============================ Executing Strict Mode ==========================="
	$(MAKE) -C tests pjd.strict
	@echo "============================ Finished Strict Mode ============================"

