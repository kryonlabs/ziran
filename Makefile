CC ?= cc
CFLAGS ?= -O2
CFLAGS += -D_GNU_SOURCE -std=c11 -Iinclude -Icmd/zir

BUILD_DIR ?= build
BIN_DIR := $(BUILD_DIR)/bin
FRONTEND := cmd/zir/zir.c cmd/zir/zir_parse.c cmd/zir/zir_text.c \
    cmd/zir/zir_token.c cmd/zir/zir_cleanup.c cmd/zir/zir_expr.c \
    cmd/zir/zir_check.c cmd/zir/zir_borrow.c cmd/zir/zir_laws.c \
    cmd/zir/zir_emit.c cmd/zir/zir_serial.c cmd/zir/zir_load.c \
    cmd/zir/zir_diagnostic.c
PORTABLE := cmd/zir/zir_bundle.c cmd/zir/zir_vm.c
HEADERS := $(wildcard cmd/zir/*.h)

.PHONY: all check clean
all: $(BIN_DIR)/ziran $(BIN_DIR)/zi-fmt $(BIN_DIR)/ziran-ir $(BIN_DIR)/ziran-c $(BIN_DIR)/ziran-go $(BIN_DIR)/ziran-cpp $(BIN_DIR)/ziran-zib

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

$(BIN_DIR)/ziran-cpp: cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c $(FRONTEND)

$(BIN_DIR)/ziran-zib: cmd/zir-zib/main.c $(FRONTEND) $(PORTABLE) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-zib/main.c $(FRONTEND) $(PORTABLE)

$(BIN_DIR)/bundle-link-test: tests/bundle_link_test.c $(FRONTEND) $(PORTABLE) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/bundle_link_test.c $(FRONTEND) $(PORTABLE)

check: all $(BIN_DIR)/bundle-link-test
	$(BIN_DIR)/bundle-link-test
	sh tests/standalone.sh $(BIN_DIR)/ziran
	sh tests/portable_strings.sh $(BIN_DIR)/ziran
	sh tests/module_paths.sh $(BIN_DIR)/ziran

clean:
	rm -rf $(BUILD_DIR)
