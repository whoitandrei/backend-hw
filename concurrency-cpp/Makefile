PKGS := 01_spinlock 02_mutex 03_semaphore 04_waitgroup 05_once 06_rwmutex 07_barrier

CXX ?= c++
CXXFLAGS := -std=c++20 -O2 -g -pthread -Wall -Wextra -Wno-unused-parameter \
            -Wno-unused-private-field -I.
RACEFLAGS := -fsanitize=thread -fno-omit-frame-pointer
BIN := bin
INTERNAL := internal/futex/futex.cpp internal/testing/testing.cpp

.PHONY: all test race stress bench vet check clean one $(PKGS) \
        spinlock mutex semaphore waitgroup once rwmutex barrier

all: check

# Всё для одного примитива: сборка с предупреждениями, тесты, гонки, стресс.
#   make mutex        или   make 02_mutex
one:
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(DIR)/*.cpp $(INTERNAL) -o $(BIN)/$(DIR)
	$(BIN)/$(DIR)
	$(CXX) $(CXXFLAGS) $(RACEFLAGS) $(DIR)/*.cpp $(INTERNAL) -o $(BIN)/$(DIR).race
	$(BIN)/$(DIR).race
	$(BIN)/$(DIR).race -count=20
	@echo "$(DIR): всё зелёное"

spinlock:  ; @$(MAKE) --no-print-directory one DIR=01_spinlock
mutex:     ; @$(MAKE) --no-print-directory one DIR=02_mutex
semaphore: ; @$(MAKE) --no-print-directory one DIR=03_semaphore
waitgroup: ; @$(MAKE) --no-print-directory one DIR=04_waitgroup
once:      ; @$(MAKE) --no-print-directory one DIR=05_once
rwmutex:   ; @$(MAKE) --no-print-directory one DIR=06_rwmutex
barrier:   ; @$(MAKE) --no-print-directory one DIR=07_barrier

$(PKGS):
	@$(MAKE) --no-print-directory one DIR=$@

build:
	@mkdir -p $(BIN)
	@for d in $(PKGS); do \
		echo "$(CXX) $(CXXFLAGS) $$d/*.cpp -o $(BIN)/$$d"; \
		$(CXX) $(CXXFLAGS) $$d/*.cpp $(INTERNAL) -o $(BIN)/$$d || exit 1; \
	done

build-race:
	@mkdir -p $(BIN)
	@for d in $(PKGS); do \
		echo "$(CXX) $(CXXFLAGS) $(RACEFLAGS) $$d/*.cpp -o $(BIN)/$$d.race"; \
		$(CXX) $(CXXFLAGS) $(RACEFLAGS) $$d/*.cpp $(INTERNAL) -o $(BIN)/$$d.race || exit 1; \
	done

test: build
	@for d in $(PKGS); do $(BIN)/$$d || exit 1; done

race: build-race
	@for d in $(PKGS); do $(BIN)/$$d.race || exit 1; done

stress: build-race
	@for d in $(PKGS); do $(BIN)/$$d.race -count=100 || exit 1; done

bench: build
	@for d in $(PKGS); do $(BIN)/$$d -run='$$^' -bench=. || exit 1; done

vet: build

check: vet race stress
	@echo "всё зелёное"

clean:
	rm -rf $(BIN)
