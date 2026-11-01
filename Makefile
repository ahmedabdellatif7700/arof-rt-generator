# Convenience wrapper around CMake and Docker. `make help` lists the targets.

BUILD_DIR  ?= build
BUILD_TYPE ?= Release
# -mcpu: cortex-a72 for a Pi 4 (this board), cortex-a76 for a Pi 5, none off aarch64
ifeq ($(shell uname -m),aarch64)
MCPU       ?= cortex-a72
else
MCPU       ?=
endif
DOCKER_MCPU ?= $(or $(MCPU),cortex-a72)
IMAGE      ?= arof-stream
CONFIG     ?= config/default.json
JOBS       ?= $(shell nproc)
# receiver of the stream (stream_rx or tools/fake_kv260.py); empty: as in CONFIG
HOST       ?=
PORT       ?=
comma      := ,
CONTAINER  ?= arof-stream
STREAM_SET := $(if $(HOST)$(PORT),--set '{"stream": {$(if $(HOST),"host": "$(HOST)")$(if $(and $(HOST),$(PORT)),$(comma) )$(if $(PORT),"port": $(PORT))}}')

.PHONY: all help configure build test run docker docker-run status clean distclean

all: build

help:
	@echo "make build        configure and build natively ($(BUILD_DIR)/, -mcpu=$(MCPU))"
	@echo "make test         build and run the unit tests"
	@echo "make run          run the native binary with CONFIG=$(CONFIG)"
	@echo "make docker       build the Docker image $(IMAGE) (MCPU=$(DOCKER_MCPU))"
	@echo "make docker-run   run the image with config/ and records/ mounted (foreground, Ctrl-C stops)"
	@echo "                  HOST=ip [PORT=n]: stream to that receiver instead of CONFIG's"
	@echo "make status       is the streamer container up: uptime, CPU/memory, its last log lines"
	@echo "make clean        remove the build tree"
	@echo "make distclean    clean, also remove records/ and the Docker image"

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DAROF_MCPU=$(MCPU)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build
	./$(BUILD_DIR)/arof_stream --config $(CONFIG)

docker:
	docker build --build-arg MCPU=$(DOCKER_MCPU) -t $(IMAGE) .

docker-run:
	mkdir -p records
	docker run --rm --name $(CONTAINER) --network host --cpus 3 --cap-add SYS_NICE \
	    -v $(CURDIR)/config:/config -v $(CURDIR)/records:/records \
	    $(IMAGE) --config /config/$(notdir $(CONFIG)) $(STREAM_SET)

status:
	@if [ -n "$$(docker ps -q --filter name=^$(CONTAINER)$$)" ]; then \
	    docker ps --filter name=^$(CONTAINER)$$ --format '$(CONTAINER) is UP: {{.Status}} (image {{.Image}}, id {{.ID}})'; \
	    docker stats --no-stream --format 'CPU {{.CPUPerc}}, memory {{.MemUsage}}, network {{.NetIO}}' $(CONTAINER); \
	    echo "last log lines:"; docker logs --tail 4 $(CONTAINER) 2>&1 | sed 's/^/  /'; \
	else \
	    echo "$(CONTAINER) is NOT running"; \
	fi

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf records
	-docker image rm $(IMAGE)
