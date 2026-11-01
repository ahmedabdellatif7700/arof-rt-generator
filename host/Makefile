# Run on the host (the laptop) that receives the Pi's stream.
#   make            build arof_rx, open the SSH tunnel to the Pi, receive CYCLES cycles, close it
# Then on the Pi:   make fake
# The Pi connects to its own 127.0.0.1:PORT; the tunnel (ssh -R, in the background while make
# runs) carries that to this host's localhost:PORT, where arof_rx listens.

PI        ?= rpi4b
PORT      ?= 5000
CYCLES    ?= 10
# upper limit in seconds from the connection (0: none); a cycle (5000 TRAIN + 44 991 TEST) takes
# about 5 s, so 10 cycles about 50 s, plus up to 5 s waiting for the first cycle boundary
DURATION  ?= 120
# a new directory per capture
ifeq ($(origin OUT),undefined)
OUT       := captures/$(shell date +%Y%m%d-%H%M%S)
endif
# the capture `make verify` re-checks: the latest one unless given
CAPTURE   ?= $(lastword $(sort $(wildcard captures/*)))

CXX       ?= g++
CXXFLAGS  ?= -O2 -std=c++17 -Wall -Wextra
LDLIBS    := -lmatio
# nlohmann/json.hpp, if not in the system includes (Fedora: json-devel)
JSON_INC  ?=
SSH_CTL   := /tmp/arof-tunnel-$(PORT).sock

.DEFAULT_GOAL := run
.PHONY: run build deps verify wireshark clean help

run: arof_rx
	@# earlier tunnels from this host to that port (an interrupted make, a manual ssh -R): end them,
	@# which frees the port on the Pi
	@ssh -S $(SSH_CTL) -O exit $(PI) 2>/dev/null; rm -f $(SSH_CTL); \
	    if pkill -f -- "^(/usr/bin/)?ssh .*-R $(PORT):localhost:$(PORT)"; then echo "closed an earlier tunnel to port $(PORT)"; sleep 1; fi
	@ssh -f -N -M -S $(SSH_CTL) -o ExitOnForwardFailure=yes -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
	    -R $(PORT):localhost:$(PORT) $(PI) \
	    || { echo "tunnel to $(PI) failed: port $(PORT) is still busy on the Pi (a tunnel from another"; \
	         echo "machine or a dead session). Free it on the Pi with: make tunnel-close"; exit 1; }
	@echo "tunnel open: $(PI):127.0.0.1:$(PORT) -> localhost:$(PORT). On the Pi: make fake"
	@trap 'ssh -S $(SSH_CTL) -O exit $(PI) 2>/dev/null; echo "tunnel closed"' EXIT; \
	    ./arof_rx --port $(PORT) --duration $(DURATION) --cycles $(CYCLES) --out $(OUT)

build: arof_rx

arof_rx: arof_rx.cpp dataset.h mat_file.h ../src/stream/frame.h
	$(CXX) $(CXXFLAGS) -I../src $(if $(JSON_INC),-I$(JSON_INC)) -o $@ arof_rx.cpp $(LDLIBS)

deps:
	sudo dnf install -y gcc-c++ make json-devel matio-devel wireshark

verify: arof_rx
	@test -n "$(CAPTURE)" || { echo "no capture in captures/"; exit 1; }
	./arof_rx --file $(CAPTURE)/stream.raw --cycles $(CYCLES) --out $(CAPTURE)/verify

wireshark:
	wireshark -i lo -f "tcp port $(PORT)" -k &

clean:
	rm -f arof_rx

help:
	@echo "make              build, tunnel to $(PI), receive $(CYCLES) cycles into captures/<time>/ (Ctrl-C stops early)"
	@echo "make verify       re-check the latest capture from its stream.raw (CAPTURE=captures/<time>)"
	@echo "make wireshark    Wireshark on lo, tcp port $(PORT) (optional, before make)"
	@echo "make deps         install g++, nlohmann json, matio, Wireshark (Fedora, once)"
	@echo "variables: PI=$(PI) PORT=$(PORT) CYCLES=$(CYCLES) DURATION=$(DURATION)"
