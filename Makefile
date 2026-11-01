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

.DEFAULT_GOAL := stream
.PHONY: stream fake help configure build test run docker docker-run status tunnel-close clean distclean

# stream to the receiver: the KV260 (stream.host in CONFIG, or HOST=)
stream: docker-run

# stream to 127.0.0.1: the SSH tunnel to the laptop's arof_rx (host/: make), the fake KV260
fake:
	$(MAKE) docker-run HOST=127.0.0.1

help:
	@echo "make             stream to the receiver in CONFIG (the KV260), or HOST=ip; Ctrl-C stops"
	@echo "make fake        stream to 127.0.0.1: through the SSH tunnel to the laptop (host/: make)"
	@echo "make build        configure and build natively ($(BUILD_DIR)/, -mcpu=$(MCPU))"
	@echo "make test         build and run the unit tests"
	@echo "make run          run the native binary with CONFIG=$(CONFIG)"
	@echo "make docker       build the Docker image $(IMAGE) (MCPU=$(DOCKER_MCPU))"
	@echo "make docker-run   (re)build the image if needed, run it with config/ and records/ mounted"
	@echo "                  (foreground, Ctrl-C stops)"
	@echo "                  HOST=ip [PORT=n]: stream to that receiver instead of CONFIG's"
	@echo "make status       is the streamer container up: uptime, CPU/memory, its last log lines"
	@echo "make tunnel-close end the SSH tunnel holding 127.0.0.1:$(or $(PORT),5000) (sudo; when the host's"
	@echo "                  make reports the port busy)"
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

docker-run: docker
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

# sshd hides whose socket it is from users, hence sudo
tunnel-close:
	@pids=$$(sudo ss -tlnpH "sport = :$(or $(PORT),5000)" | grep sshd | grep -o 'pid=[0-9]*' | cut -d= -f2 | sort -u); \
	if [ -n "$$pids" ]; then sudo kill $$pids && echo "closed the tunnel on port $(or $(PORT),5000) (sshd pid $$pids)"; \
	else echo "no tunnel on port $(or $(PORT),5000)"; fi

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf records
	-docker image rm $(IMAGE)
