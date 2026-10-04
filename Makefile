CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SDK_PATH := $(shell xcrun --show-sdk-path)
CXXFLAGS += -isysroot $(SDK_PATH) -I$(SDK_PATH)/usr/include/c++/v1
endif

PROTO_SRCS = components/pm5_ftms/protocol.cpp
TEST_SRCS = tests/test_protocol.cpp

build/test_protocol: $(TEST_SRCS) $(PROTO_SRCS) components/pm5_ftms/protocol.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Icomponents/pm5_ftms $(TEST_SRCS) $(PROTO_SRCS) -o $@

.PHONY: test
test: build/test_protocol
	./build/test_protocol

.PHONY: clean
clean:
	rm -rf build
