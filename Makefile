CC ?= cc
CFLAGS ?= -O2
CFLAGS += -D_GNU_SOURCE -std=c11 -Iinclude -Icmd/zir

BUILD_DIR ?= build
BIN_DIR := $(BUILD_DIR)/bin
FRONTEND := cmd/zir/zir.c cmd/zir/zir_parse.c cmd/zir/zir_text.c \
    cmd/zir/zir_token.c cmd/zir/zir_cleanup.c cmd/zir/zir_expr.c \
    cmd/zir/zir_check.c cmd/zir/zir_borrow.c cmd/zir/zir_laws.c \
    cmd/zir/zir_emit.c cmd/zir/zir_style_imports.c \
    cmd/zir/zir_diagnostic.c
HEADERS := $(wildcard cmd/zir/*.h)

.PHONY: all check clean
all: $(BIN_DIR)/ziran $(BIN_DIR)/zi-fmt $(BIN_DIR)/ziran-ir $(BIN_DIR)/ziran-c $(BIN_DIR)/ziran-go

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/ziran: scripts/ziran | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/zi-fmt: scripts/zi-fmt.sh | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/ziran-ir: cmd/zir-ir/main.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-ir/main.c $(FRONTEND)

$(BIN_DIR)/ziran-c: $(wildcard cmd/zir-c/*.c) $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-c/main.c cmd/zir-c/zir_c_lower.c \
	    cmd/zir-c/zir_c_plan9.c $(FRONTEND)

$(BIN_DIR)/ziran-go: cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c $(FRONTEND)

check: all
	sh tests/standalone.sh $(BIN_DIR)/ziran

clean:
	rm -rf $(BUILD_DIR)
