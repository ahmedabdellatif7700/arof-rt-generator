function export_dataset(vpp_mv, rop_dbm, out_file)
%EXPORT_DATASET  One A-RoF sequence as a training dataset in the layout of
% nn_training_model/data/training_data.mat (X: N x 4 inputs, Y: N x 2 targets), for the
% 4-12-2 engine and nn_training_model/equalizer_q8_24.py --dataset.
%   export_dataset                          300 mV, -11.75 dBm, results/arof_training_data.mat
%   export_dataset(vpp_mv, rop_dbm, out_file)
% Link, seed and receiver as the study (core/experiment_setup.m, link/transmit_receive.m).
% Sample n: X = [Re x(n) Im x(n) Re x(n-1) Im x(n-1)] (newest symbol first), Y = sent
% symbol d(n). Symbols on the integer 16QAM grid {+-1, +-3}: d and x times sqrt(10).
% 9999 symbols -> 9998 samples, so the 80/20 split gives 7998 + 2000 as the engine's ROMs.
% Run from matlab/: export_dataset

if nargin < 1, vpp_mv = 300; end
if nargin < 2, rop_dbm = -11.75; end
here = fileparts(fileparts(mfilename('fullpath')));
if nargin < 3, out_file = fullfile(here, 'results', 'arof_training_data.mat'); end
addpath(fullfile(here, 'core'), fullfile(here, 'link'));

[cfg, link] = experiment_setup();
cfg.n_sym  = 9999;
cfg.n_runs = 1;
[x, d, idx] = simulate_link(cfg, link, [vpp_mv rop_dbm]);

grid = sqrt(10);                                        % unit power -> {+-1, +-3}
rx = grid * x;  tx = round(grid * d);
X = [real(rx(2:end)), imag(rx(2:end)), real(rx(1:end-1)), imag(rx(1:end-1))];
Y = int16([real(tx(2:end)), imag(tx(2:end))]);

bits = reshape(de2bi(idx, 4, 'left-msb').', [], 1);    % Gray labels as qammod
evm = sqrt(mean(abs(x - d).^2) / mean(abs(d).^2));

S = struct('X', X, 'Y', Y, ...
    'rx_aligned', rx, 'tx_aligned', complex(tx), 'tx_symbols', complex(tx), ...
    'tx_indices', uint8(idx), 'tx_bits', uint8(bits), ...
    'mod_order', uint8(16), 'bits_per_symbol', uint8(4), 'delay', uint8(1), ...
    'equalizer_taps', uint8(2), 'equalizer_input_size', uint8(4), ...
    'equalizer_output_size', uint8(2), ...
    'vpp_mv', vpp_mv, 'rop_dbm', rop_dbm, 'seed', cfg.seed, 'evm_no_eq', evm, ...
    'source', 'A-RoF 60 GHz link (Liu JLT 2017), matlab/link/transmit_receive.m');
save(out_file, '-struct', 'S');
fprintf('%s: %d samples, %d mV, %.2f dBm, seed %d, no-EQ EVM %.1f %%\n', ...
    out_file, size(X, 1), vpp_mv, rop_dbm, cfg.seed, 100*evm);
end
