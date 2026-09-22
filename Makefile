CC ?= cc
CFLAGS ?= -O2
CFLAGS += -D_GNU_SOURCE -std=c11 -Iinclude -Icmd/kir

BUILD_DIR ?= build
BIN_DIR := $(BUILD_DIR)/bin
FRONTEND := cmd/kir/kir.c cmd/kir/kir_parse.c cmd/kir/kir_text.c \
    cmd/kir/kir_token.c cmd/kir/kir_cleanup.c cmd/kir/kir_expr.c \
    cmd/kir/kir_check.c cmd/kir/kir_borrow.c cmd/kir/kir_laws.c \
    cmd/kir/kir_emit.c cmd/kir/kir_style_imports.c \
    cmd/kir/kir_diagnostic.c
HEADERS := $(wildcard cmd/kir/*.h)

.PHONY: all check clean
all: $(BIN_DIR)/ziran $(BIN_DIR)/ziran-ir $(BIN_DIR)/ziran-c $(BIN_DIR)/ziran-go

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/ziran: scripts/ziran | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/ziran-ir: cmd/k2kir/main.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/k2kir/main.c $(FRONTEND)

$(BIN_DIR)/ziran-c: $(wildcard cmd/k2c/*.c) $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/k2c/main.c cmd/k2c/k2c_lower.c \
	    cmd/k2c/k2c_plan9.c $(FRONTEND)

$(BIN_DIR)/ziran-go: cmd/k2go/main.c cmd/k2go/k2go_lower.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/k2go/main.c cmd/k2go/k2go_lower.c $(FRONTEND)

check: all
	sh tests/standalone.sh $(BIN_DIR)/ziran

clean:
	rm -rf $(BUILD_DIR)
