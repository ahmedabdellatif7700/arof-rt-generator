# Run on the host (the laptop) that receives the Pi's stream through an SSH tunnel.
# `make help` lists the steps; README.md explains them.

PI        ?= rpi4b
PORT      ?= 5000
DURATION  ?= 60
SAMPLES   ?=
# complete 7998 TRAIN + 2000 TEST cycles to keep as datasets (cycle_000/ ...); empty: none
CYCLES    ?=
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

.PHONY: help deps build tunnel receive wireshark verify clean

help:
	@echo "once:"
	@echo "  make deps        install the tools (Fedora: g++, nlohmann json, matio, Wireshark, ncat)"
	@echo "  make build       build arof_rx, the receiver"
	@echo "each capture, in this order:"
	@echo "  make wireshark   terminal 3: Wireshark on lo, filter tcp port $(PORT) (optional)"
	@echo "  make receive     terminal 2: arof_rx on port $(PORT), $(DURATION) s, into captures/<time>/"
	@echo "  make tunnel      terminal 1: log in to $(PI) with the tunnel; in that shell, on the Pi:"
	@echo "                     cd ~/dev/arof-rt-generator"
	@echo "                     make docker docker-run HOST=127.0.0.1$(if $(filter-out 5000,$(PORT)), PORT=$(PORT))"
	@echo "                   (make status in another Pi shell: is the streamer up; Ctrl-C on the Pi stops it)"
	@echo "any time:"
	@echo "  make verify      re-check a capture from its stream.raw (CAPTURE=captures/<time>, default the latest)"
	@echo "variables: PI=$(PI) PORT=$(PORT) DURATION=$(DURATION) (0: until END) SAMPLES=<max>"
	@echo "           CYCLES=K: also keep K complete, aligned 7998 + 2000 cycles as datasets (1 cycle = 1 s)"

deps:
	sudo dnf install -y gcc-c++ make json-devel matio-devel wireshark nmap-ncat
	@echo "for Wireshark without root: sudo usermod -aG wireshark $$USER, then log out and in"

build: arof_rx

arof_rx: arof_rx.cpp dataset.h mat_file.h ../src/stream/frame.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ arof_rx.cpp $(LDLIBS)

# an SSH login to the Pi that also listens on the Pi's 127.0.0.1:PORT and carries what arrives
# there to this host's localhost:PORT, where arof_rx listens; open while the shell is
tunnel:
	ssh -R $(PORT):localhost:$(PORT) $(PI)

receive: arof_rx
	./arof_rx --port $(PORT) --duration $(DURATION) $(if $(SAMPLES),--samples $(SAMPLES)) $(if $(CYCLES),--cycles $(CYCLES)) --out $(OUT)

wireshark:
	wireshark -i lo -f "tcp port $(PORT)" -k &

verify: arof_rx
	@test -n "$(CAPTURE)" || { echo "no capture in captures/"; exit 1; }
	./arof_rx --file $(CAPTURE)/stream.raw $(if $(CYCLES),--cycles $(CYCLES)) --out $(CAPTURE)/verify

clean:
	rm -f arof_rx
