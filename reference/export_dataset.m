function export_dataset(vpp_mv, rop_dbm, out_file, taps, scale)
%EXPORT_DATASET  One A-RoF sequence as a training dataset in the layout of
% nn_training_model/data/training_data.mat (X: N x 2*taps inputs, Y: N x 2 targets), for the
% engine's target network, the 6-12-2 tanh (own_tanh_6 in core/equalizer_defs.m).
%   export_dataset                          300 mV, -11.75 dBm, 3 taps, unit power,
%                                           results/arof_training_data.mat
%   export_dataset(vpp_mv, rop_dbm, out_file, taps, scale)
% Link, seed and receiver as the study (core/experiment_setup.m, link/transmit_receive.m).
% Samples as equalizer.py (model 'real') builds them: window x(k), x(k-1), ..., x(k-taps+1),
% newest first, I and Q interleaved; target the center tap d(k - delay), delay = floor(taps/2).
% scale: 'unit' (unit-power symbols, as equalizer.py for sigmoid and tanh) or 'grid'
% (x sqrt(10): the 16QAM grid {+-1, +-3}, as equalizer.py for the multilevel activations).
% n_sym = 9997 + taps symbols -> 9998 samples, so the 80/20 split gives 7998 + 2000.
% Run from matlab/: export_dataset

if nargin < 1, vpp_mv = 300; end
if nargin < 2, rop_dbm = -11.75; end
here = fileparts(fileparts(mfilename('fullpath')));
if nargin < 3, out_file = fullfile(here, 'results', 'arof_training_data.mat'); end
if nargin < 4, taps = 3; end
if nargin < 5, scale = 'unit'; end
addpath(fullfile(here, 'core'), fullfile(here, 'link'));

[cfg, link] = experiment_setup();
cfg.n_sym  = 9997 + taps;
cfg.n_runs = 1;
[x, d, idx] = simulate_link(cfg, link, [vpp_mv rop_dbm]);

switch scale
    case 'unit', g = 1;
    case 'grid', g = sqrt(10);
    otherwise, error('scale must be ''unit'' or ''grid''');
end
rx = g * x;  tx = g * d;
delay = floor(taps / 2);
k = (taps:cfg.n_sym).';                                 % newest symbol of each window
X = zeros(numel(k), 2*taps);
for m = 0:taps-1                                        % [I(k) Q(k) I(k-1) Q(k-1) ...]
    X(:, 2*m + 1) = real(rx(k - m));
    X(:, 2*m + 2) = imag(rx(k - m));
end
Y = [real(tx(k - delay)), imag(tx(k - delay))];         % center tap

bits = reshape(de2bi(idx, 4, 'left-msb').', [], 1);    % Gray labels as qammod
evm = sqrt(mean(abs(x - d).^2) / mean(abs(d).^2));

S = struct('X', X, 'Y', Y, ...
    'rx_aligned', rx, 'tx_aligned', tx, 'tx_symbols', tx, ...
    'tx_indices', uint8(idx), 'tx_bits', uint8(bits), ...
    'mod_order', uint8(16), 'bits_per_symbol', uint8(4), 'delay', uint8(delay), ...
    'equalizer_taps', uint8(taps), 'equalizer_input_size', uint8(2*taps), ...
    'equalizer_output_size', uint8(2), 'scale', scale, ...
    'vpp_mv', vpp_mv, 'rop_dbm', rop_dbm, 'seed', cfg.seed, 'evm_no_eq', evm, ...
    'source', 'A-RoF 60 GHz link (Liu JLT 2017), matlab/link/transmit_receive.m');
save(out_file, '-struct', 'S');
fprintf('%s: %d samples x %d inputs, %d taps, delay %d, %s scale, %d mV, %.2f dBm, seed %d, no-EQ EVM %.1f %%\n', ...
    out_file, size(X, 1), size(X, 2), taps, delay, scale, vpp_mv, rop_dbm, cfg.seed, 100*evm);
end
