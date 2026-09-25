CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CFLAGS += -D_GNU_SOURCE -std=c11 -Iinclude -Icmd/zir

BUILD_DIR ?= build
BIN_DIR := $(BUILD_DIR)/bin
FRONTEND := cmd/zir/zir.c cmd/zir/zir_enum.c cmd/zir/zir_parse.c cmd/zir/zir_text.c \
    cmd/zir/zir_token.c cmd/zir/zir_cleanup.c cmd/zir/zir_expr.c \
    cmd/zir/zir_check.c cmd/zir/zir_borrow.c \
    cmd/zir/zir_emit.c cmd/zir/zir_serial.c cmd/zir/zir_load.c \
    cmd/zir/zir_diagnostic.c
PORTABLE := cmd/zir/zir_bundle.c cmd/zir/zir_vm.c
HEADERS := $(wildcard cmd/zir/*.h) $(wildcard include/*.h)
LIB_SOURCES := $(FRONTEND) $(PORTABLE) cmd/zir/zir_host.c
LIB_OBJECTS := $(patsubst cmd/zir/%.c,$(BUILD_DIR)/obj/%.o,$(LIB_SOURCES))

.PHONY: all check clean
CHECK_JOBS ?= 4
all: $(BIN_DIR)/ziran $(BIN_DIR)/zi-fmt $(BIN_DIR)/zi2zir $(BIN_DIR)/zi-inspect $(BIN_DIR)/zi2c $(BIN_DIR)/zi2go $(BIN_DIR)/zi2cpp $(BIN_DIR)/zi2zib $(BUILD_DIR)/libziran.a

$(BUILD_DIR)/obj/%.o: cmd/zir/%.c $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/libziran.a: $(LIB_OBJECTS) Makefile
	$(RM) $@
	$(AR) rcs $@ $(LIB_OBJECTS)

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/ziran: scripts/ziran | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/zi-fmt: scripts/zi-fmt.sh | $(BIN_DIR)
	cp $< $@
	chmod +x $@

$(BIN_DIR)/zi2zir: cmd/zir-ir/main.c cmd/zir/zir_bundle.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-ir/main.c cmd/zir/zir_bundle.c $(FRONTEND)

$(BIN_DIR)/zi-inspect: cmd/zir-inspect/main.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-inspect/main.c $(FRONTEND)

$(BIN_DIR)/zi2c: $(wildcard cmd/zir-c/*.c) cmd/zir/zir_bundle.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-c/main.c cmd/zir-c/zir_c_lower.c \
	    cmd/zir-c/zir_c_plan9.c cmd/zir/zir_bundle.c $(FRONTEND)

$(BIN_DIR)/zi2go: cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c cmd/zir/zir_bundle.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-go/main.c cmd/zir-go/zir_go_lower.c cmd/zir/zir_bundle.c $(FRONTEND)

$(BIN_DIR)/zi2cpp: cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c cmd/zir/zir_bundle.c $(FRONTEND) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-cpp/main.c cmd/zir-cpp/zir_cpp_lower.c cmd/zir/zir_bundle.c $(FRONTEND)

$(BIN_DIR)/zi2zib: cmd/zir-zib/main.c $(BUILD_DIR)/libziran.a $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cmd/zir-zib/main.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/bundle-link-test: tests/bundle_link_test.c $(FRONTEND) $(PORTABLE) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/bundle_link_test.c $(FRONTEND) $(PORTABLE)

$(BIN_DIR)/host-capability-test: tests/host_capability_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/host_capability_test.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/record-host-test: tests/record_host_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/record_host_test.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/process-host-test: tests/process_host_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/process_host_test.c $(BUILD_DIR)/libziran.a

$(BIN_DIR)/slice-host-test: tests/host_slice_test.c $(BUILD_DIR)/libziran.a | $(BIN_DIR)
	$(CC) -D_GNU_SOURCE -std=c11 -Iinclude -o $@ tests/host_slice_test.c $(BUILD_DIR)/libziran.a

check: all $(BIN_DIR)/bundle-link-test $(BIN_DIR)/host-capability-test $(BIN_DIR)/record-host-test $(BIN_DIR)/slice-host-test $(BIN_DIR)/process-host-test
	python3 tests/run_check.py --bin-dir $(BIN_DIR) --jobs $(CHECK_JOBS)

clean:
	rm -rf $(BUILD_DIR)
