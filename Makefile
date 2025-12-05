CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter
FUSE_CFLAGS = $(shell pkg-config fuse3 --cflags 2>/dev/null || echo "-I/usr/include/fuse3 -D_FILE_OFFSET_BITS=64")
FUSE_LIBS = $(shell pkg-config fuse3 --libs 2>/dev/null || echo "-lfuse3")
LDFLAGS = $(FUSE_LIBS) -lpthread
SRC = src/main.cpp
BIN = kubsh

.PHONY: all build run deb clean

all: build

# Компиляция из исходников
build:
	$(CXX) $(CXXFLAGS) $(FUSE_CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

# Запуск kubsh
run: build
	./$(BIN)

# Сборка deb-пакета
deb: build
	dpkg-buildpackage -us -uc

# Очистка собранного бинарника
clean:
	rm -f $(BIN)
