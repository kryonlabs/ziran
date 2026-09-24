CC ?= cc
AR ?= ar
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
HEADERS := $(wildcard cmd/zir/*.h) $(wildcard include/*.h)
LIB_SOURCES := $(FRONTEND) $(PORTABLE) cmd/zir/zir_host.c
LIB_OBJECTS := $(patsubst cmd/zir/%.c,$(BUILD_DIR)/obj/%.o,$(LIB_SOURCES))

.PHONY: all check clean
all: $(BIN_DIR)/ziran $(BIN_DIR)/zi-fmt $(BIN_DIR)/zi2zir $(BIN_DIR)/zi2c $(BIN_DIR)/zi2go $(BIN_DIR)/zi2cpp $(BIN_DIR)/zi2zib $(BUILD_DIR)/libziran.a

$(BUILD_DIR)/obj/%.o: cmd/zir/%.c $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/libziran.a: $(LIB_OBJECTS)
	$(AR) rcs $@ $^

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/ziran: scripts/ziran | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/zi-fmt: scripts/zi-fmt.sh | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/zi2zir: cmd/zir-ir/main.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-ir/main.c $(FRONTEND)

$(BIN_DIR)/zi2c: $(wildcard cmd/zir-c/*.c) $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-c/main.c cmd/zir-c/zir_c_lower.c \
	    cmd/zir-c/zir_c_plan9.c $(FRONTEND)

$(BIN_DIR)/zi2go: cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c $(FRONTEND)

$(BIN_DIR)/zi2cpp: cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c $(FRONTEND)

$(BIN_DIR)/zi2zib: cmd/zir-zib/main.c $(BUILD_DIR)/libziran.a $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-zib/main.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/bundle-link-test: tests/bundle_link_test.c $(FRONTEND) $(PORTABLE) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/bundle_link_test.c $(FRONTEND) $(PORTABLE)

$(BIN_DIR)/host-capability-test: tests/host_capability_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/host_capability_test.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/record-host-test: tests/record_host_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/record_host_test.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/slice-host-test: tests/host_slice_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/host_slice_test.c $(BUILD_DIR)/libziran.a

check: all $(BIN_DIR)/bundle-link-test $(BIN_DIR)/host-capability-test $(BIN_DIR)/record-host-test $(BIN_DIR)/slice-host-test
	$(BIN_DIR)/bundle-link-test
	sh tests/standalone.sh $(BIN_DIR)/ziran
	sh tests/portable_strings.sh $(BIN_DIR)/ziran
	sh tests/portable_i64.sh $(BIN_DIR)/ziran
	sh tests/portable_arrays.sh $(BIN_DIR)/ziran
	sh tests/portable_slices.sh $(BIN_DIR)/ziran
	sh tests/portable_globals.sh $(BIN_DIR)/ziran
	sh tests/module_paths.sh $(BIN_DIR)/ziran
	sh tests/language_contract.sh $(BIN_DIR)/ziran
	sh tests/slots.sh $(BIN_DIR)/ziran
	sh tests/native_nil.sh $(BIN_DIR)/ziran
	sh tests/host_capability.sh $(BIN_DIR)/ziran $(BIN_DIR)/host-capability-test
	sh tests/portable_host_records.sh $(BIN_DIR)/ziran $(BIN_DIR)/record-host-test
	sh tests/host_slices.sh $(BIN_DIR)/ziran $(BIN_DIR)/slice-host-test

clean:
	rm -rf $(BUILD_DIR)
