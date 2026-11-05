Raspberry Pi A-RoF generator: reference files from the MATLAB repository

main_demo.m              the MATLAB experiment: the link (steps 1-7) the generator ports, the
                         equalizers (step 8), EVM and BER (step 9)
equalizer.py             matlab/equalizer.py: trains the networks of step 8. Its 'real' model
                         builds the samples the generator streams (3 taps, center tap, unit power)
export_dataset.m         matlab/analysis/export_dataset.m: writes arof_training_data.mat

arof_training_data.mat   (matlab/results/arof_training_data.mat, as exported by export_dataset.m)
    The A-RoF link: 16QAM, 300 mV, -11.75 dBm ROP, seed 2017, 10000 symbols, no-EQ EVM 13.3 %.
      X (9998 x 6): [Re x(k) Im x(k) Re x(k-1) Im x(k-1) Re x(k-2) Im x(k-2)], newest first
      Y (9998 x 2): the sent symbol at the center tap, d(k-1), unit power
                    (levels +-1/sqrt(10), +-3/sqrt(10): 16QAM normalized to unit average power)
    Extra fields: rx_aligned, tx_aligned, tx_symbols, tx_indices, tx_bits, mod_order,
    bits_per_symbol, delay (1), equalizer_taps (3), equalizer_input_size (6),
    equalizer_output_size (2), scale ('unit'), vpp_mv, rop_dbm, seed, evm_no_eq, source.
    The generator's recordings and captures have this layout. Its size (9998 samples) is that of
    this export; the dataset the project uses mirrors main_demo.m step 8: 49 991 samples, the first
    5000 to train and the next 44 991 to test (the stream's `demo` schedule).

These are copies; after the merge into the main project they are replaced by the originals
(docs/INTEGRATION_HANDOFF.md section 3).
