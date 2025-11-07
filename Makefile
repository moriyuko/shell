CXX = g++
CXXFLAGS = -std=c++17
SRC = src/main.cpp
BIN = kubsh

.PHONY: all build run deb clean

all: build

# Компиляция из исходников
build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

# Запуск kubsh
run: build
	./$(BIN)

# Сборка deb-пакета
deb: build
	dpkg-buildpackage -us -uc

# Очистка собранного бинарника
clean:
	rm -f $(BIN)
