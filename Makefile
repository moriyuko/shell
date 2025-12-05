CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra $(shell pkg-config fuse3 --cflags)
LDFLAGS = $(shell pkg-config fuse3 --libs) -lpthread
SRC = src/main.cpp
BIN = kubsh

.PHONY: all build run deb clean

all: build

# Компиляция из исходников
build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

# Запуск kubsh
run: build
	./$(BIN)

# Сборка deb-пакета
deb: build
	dpkg-buildpackage -us -uc

# Очистка собранного бинарника
clean:
	rm -f $(BIN)
