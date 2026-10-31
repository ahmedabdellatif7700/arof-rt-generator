// matlab/link/default_tx.m: transmitter and capture settings of the Liu et al. (JLT 2017)
// setup: 16QAM, 0.4 GBd, RRC roll-off 0.75, IF 0.8 GHz, simulated at the scope's 20 GS/s.
#pragma once

#include <cmath>

namespace arof {

struct TxConfig {
    int M = 16;
    double rs = 0.4e9;          // symbol rate
    double fs = 20e9;           // scope sampling rate
    double f_if = 0.8e9;
    double rolloff = 0.75;
    int rrc_span = 16;          // symbols

    int sps() const { return static_cast<int>(std::lround(fs / rs)); }   // 50
};

inline TxConfig default_tx() { return TxConfig{}; }

}  // namespace arof
