.PHONY: all test build deb clean

# Автоматически определяем путь к Python
PYTHON = python3

# Основная цель по умолчанию
all: build test

# Сборка через CMake (как у вас уже работает)
build:
	mkdir -p build
	cd build && cmake .. && make
	ln -sf build/kubsh kubsh  # Симлинк для тестов

# Запуск тестов (как они написаны)
test: build
	$(PYTHON) -m pytest -v

# Сборка DEB пакета
deb:
	dpkg-buildpackage -us -uc -b

# Очистка
clean:
	rm -rf build
	rm -f kubsh

# Для CI (специальная цель)
ci: build
	$(PYTHON) -m pytest -v --cov=. --cov-report=xml