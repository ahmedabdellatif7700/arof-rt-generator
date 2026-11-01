# Run on the host (the laptop) that receives the Pi's stream through an SSH tunnel.
# `make help` lists the steps; README.md explains them.

PI        ?= rpi4b
PI_REPO   ?= ~/dev/arof-rt-generator
PORT      ?= 5000
DURATION  ?= 60
SAMPLES   ?=
# a new directory per capture (fixed once per make run)
ifeq ($(origin OUT),undefined)
OUT       := captures/$(shell date +%Y%m%d-%H%M%S)
endif
# the capture `make verify` re-checks: the latest one unless given
CAPTURE   ?= $(lastword $(sort $(wildcard captures/*)))

CXX       ?= g++
CXXFLAGS  ?= -O2 -std=c++17 -Wall -Wextra
JSON_INC  ?=
INCLUDES  := -I../src $(if $(JSON_INC),-I$(JSON_INC))
LDLIBS    := -lmatio

.PHONY: help deps build tunnel pi-stream pi-status receive wireshark verify clean

help:
	@echo "once:"
	@echo "  make deps        install the tools (Fedora: g++, nlohmann json, matio, Wireshark, ncat)"
	@echo "  make build       build arof_rx, the receiver"
	@echo "each capture, in this order:"
	@echo "  make wireshark   terminal 3: Wireshark on lo, filter tcp port $(PORT) (optional)"
	@echo "  make receive     terminal 2: arof_rx on port $(PORT), $(DURATION) s, into captures/<time>/"
	@echo "  make pi-stream   terminal 1: SSH to $(PI) with the tunnel, start the Dockerized streamer;"
	@echo "                   Ctrl-C stops the streamer (END frame) and closes the tunnel"
	@echo "    or: make tunnel (only the tunnel) and start the streamer on the Pi yourself:"
	@echo "        make docker docker-run HOST=127.0.0.1 PORT=$(PORT)"
	@echo "any time:"
	@echo "  make pi-status   is the streamer container up on the Pi"
	@echo "  make verify      re-check a capture from its stream.raw (CAPTURE=captures/<time>, default the latest)"
	@echo "variables: PI=$(PI) PI_REPO=$(PI_REPO) PORT=$(PORT) DURATION=$(DURATION) (0: until END) SAMPLES=<max>"

deps:
	sudo dnf install -y gcc-c++ make json-devel matio-devel wireshark nmap-ncat
	@echo "for Wireshark without root: sudo usermod -aG wireshark $$USER, then log out and in"

build: arof_rx

arof_rx: arof_rx.cpp dataset.h mat_file.h ../src/stream/frame.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ arof_rx.cpp $(LDLIBS)

# the Pi connects to its 127.0.0.1:PORT; ssh carries it to this host's localhost:PORT
tunnel:
	ssh -N -R $(PORT):localhost:$(PORT) $(PI)

pi-stream:
	ssh -t -R $(PORT):localhost:$(PORT) $(PI) 'cd $(PI_REPO) && make docker docker-run HOST=127.0.0.1 PORT=$(PORT)'

pi-status:
	ssh $(PI) 'cd $(PI_REPO) && make status'

receive: arof_rx
	./arof_rx --port $(PORT) --duration $(DURATION) $(if $(SAMPLES),--samples $(SAMPLES)) --out $(OUT)

wireshark:
	wireshark -i lo -f "tcp port $(PORT)" -k &

verify: arof_rx
	@test -n "$(CAPTURE)" || { echo "no capture in captures/"; exit 1; }
	./arof_rx --file $(CAPTURE)/stream.raw --out $(CAPTURE)/verify

clean:
	rm -f arof_rx
