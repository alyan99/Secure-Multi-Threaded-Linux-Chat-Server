CC = gcc
PKG_CONFIG ?= pkg-config
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude -Iserver $(shell $(PKG_CONFIG) --cflags libsodium)
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -O2
CFLAGS += -pthread
LDLIBS += $(shell $(PKG_CONFIG) --libs libsodium) -pthread
BUILD_DIR := build
SERVER_SRC := server/server.c server/thread_pool.c server/auth.c server/security.c server/chat.c server/logger.c include/protocol.c
SERVER_OBJ := $(SERVER_SRC:%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJ := $(BUILD_DIR)/client/client.o $(BUILD_DIR)/include/protocol.o
TEST_NAMES := test_protocol test_security test_chat test_auth test_thread_pool test_network_io
TEST_BIN := $(addprefix $(BUILD_DIR)/,$(TEST_NAMES))
TEST_COMMON := $(BUILD_DIR)/include/protocol.o $(BUILD_DIR)/server/security.o $(BUILD_DIR)/server/chat.o $(BUILD_DIR)/server/auth.o $(BUILD_DIR)/server/thread_pool.o $(BUILD_DIR)/server/logger.o

.PHONY: all server client check-deps test unit integration load clean debug asan tsan valgrind
all: check-deps server client
check-deps:
	@$(PKG_CONFIG) --exists libsodium || { echo 'Missing libsodium-dev or pkg-config; see docs/SETUP.md'; exit 1; }
server: server_app
client: client_app
server_app: $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
client_app: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/test_%: $(BUILD_DIR)/test/test_%.o $(TEST_COMMON)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
unit: $(TEST_BIN)
	@set -e; for test in $(TEST_BIN); do echo "Running $$test"; "$$test"; done
integration: all
	python3 test/test_foundation.py
	python3 test/test_integration.py

test: all unit integration
load: all
	python3 test/test_load.py

debug:
	$(MAKE) clean
	$(MAKE) all CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -g3 -O0 -pthread'
asan:
	$(MAKE) clean
	$(MAKE) test CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -g -O1 -pthread -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'
tsan:
	$(MAKE) clean
	$(MAKE) test CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -g -O1 -pthread -fsanitize=thread -fno-omit-frame-pointer' LDFLAGS='-fsanitize=thread'
valgrind:
	$(MAKE) clean
	$(MAKE) all $(TEST_BIN) CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -g -O0 -pthread'
	@set -e; for test in $(TEST_BIN); do valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect --error-exitcode=99 "$$test"; done
	CHAT_VALGRIND=1 python3 test/test_integration.py
clean:
	rm -rf $(BUILD_DIR) server_app client_app

-include $(SERVER_OBJ:.o=.d) $(CLIENT_OBJ:.o=.d) $(addprefix $(BUILD_DIR)/test/,$(addsuffix .d,$(TEST_NAMES)))
