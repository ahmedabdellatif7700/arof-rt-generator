Raspberry Pi A-RoF generator: handoff package

rpi_stream_generator_handoff.md   implementation plan (C++, Docker on the Pi, TCP to the KV260)
main_demo.m, DEMO.md              the MATLAB link the generator ports (steps 1-7) and how to run it
export_dataset.m                  MATLAB script that wrote arof_training_data.mat (matlab/analysis/)

arof_training_data.mat   (matlab/results/arof_training_data.mat, unchanged)
    The A-RoF link we simulate: 16QAM, 300 mV, -11.75 dBm ROP, seed 2017, 9999 symbols,
    no-EQ EVM 13.3 %.
      X (9998 x 4): [Re x(n) Im x(n) Re x(n-1) Im x(n-1)], newest symbol first
      Y (9998 x 2): sent 16QAM symbol d(n), int16, integer grid {-3,-1,1,3} (16 points)
      80/20 sequential split -> 7998 train + 2000 test (the engine's ROM sizes)
    Extra fields: rx_aligned, tx_aligned, tx_bits, tx_indices, vpp_mv, rop_dbm, seed,
    evm_no_eq, source. The generator's recordings must match this layout.

Use with the Python model:
    python nn_training_model/equalizer_q8_24.py --dataset arof_training_data.mat
