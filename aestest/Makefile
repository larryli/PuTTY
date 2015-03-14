# Makefile for unit tests
#
# @author kryukov@frtk.ru
# @version 4.3
#
# For Putty AES NI project
# http://putty-aes-ni.googlecode.com/

CC=gcc
CFLAGS=-O2 -Wall -Werror -std=c89 -pedantic -Wno-long-long
LDFLAGS=
PUTTY=..
OBJ_DIR=obj
BIN_DIR=bin
TXT_DIR=txt
SRC_DIR=src
HEADERS=$(SRC_DIR)/coverage.h $(SRC_DIR)/defines.h
MAKEFILE=Makefile

ifeq ($(SDE), yes)
	SDERUN=sde --
else
	SDERUN=
endif

.SECONDARY:
	

$(BIN_DIR)/sshaes-%: $(OBJ_DIR)/sshaes.o $(OBJ_DIR)/aes%.o $(OBJ_DIR)/puttymem.o
	@echo "[$(LD)] $@"
	@$(CC) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/sshaesni-%: $(OBJ_DIR)/sshaesni.o $(OBJ_DIR)/aes%.o $(OBJ_DIR)/puttymem.o
	@echo "[$(LD)] $@"
	@$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/sshaes.o: $(PUTTY)/sshaes.c $(HEADERS) $(MAKEFILE)
	@echo "[$(CC)] $@"
	@$(CC) $(CFLAGS) $< -c -o $@ -I $(PUTTY) -maes -msse4 -D_FORCE_SOFTWARE_AES
   
$(OBJ_DIR)/sshaesni.o: $(PUTTY)/sshaes.c $(HEADERS) $(MAKEFILE)
	@echo "[$(CC)] $@"
	@$(CC) $(CFLAGS) $< -c -o $@ -I $(PUTTY) -maes -msse4
   
$(OBJ_DIR)/aesdemo-decode.o: $(SRC_DIR)/aesdemo.c $(HEADERS) $(MAKEFILE)
	@echo "[$(CC)] $@"
	@$(CC) $(CFLAGS) $< -c -o $@ -DDECODE

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) $(MAKEFILE)
	@echo "[$(CC)] $@"
	@$(CC) $(CFLAGS) $< -c -o $@

$(BIN_DIR)/cpuid: $(SRC_DIR)/cpuid.c $(MAKEFILE)
	@echo "[$(CC)] $@"
	@$(CC) $(CFLAGS) $< -o $@ -DSILENT

$(TXT_DIR)/test-original-%.txt: $(BIN_DIR)/sshaes-test
	@echo "[run] $@"
	@$^ $*
	@mv test-output.txt $@

$(TXT_DIR)/test-output-%.txt: $(BIN_DIR)/sshaesni-test
	@echo "[run] $@"
	@$(SDERUN) $^ $*
	@mv test-output.txt $@

$(TXT_DIR)/perf-original.txt: $(BIN_DIR)/sshaes-perf
	@echo "[run] $@"
	@$^
	@mv perf-output.txt $@

$(TXT_DIR)/perf-output.txt: $(BIN_DIR)/sshaesni-perf
	@echo "[run] $@"
	@$(SDERUN) $^
	@mv perf-output.txt $@

$(TXT_DIR)/%.sorted.txt: $(TXT_DIR)/%.txt
	@echo "[sort] $@"
	@sort $^ > $@
 
clean:
	rm $(BIN_DIR) $(OBJ_DIR) $(TXT_DIR) -rf
