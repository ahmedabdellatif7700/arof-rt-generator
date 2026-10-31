% Demo of the 60-GHz A-RoF link of Liu et al. (JLT 2017) and its equalizers, step by
% step: from the transmitted bits to the equalized symbols and their EVM and BER.
% The link (steps 1-7) is written out here with the stages and parameters of link/ and
% link/default_link.m, in the order of the link. The equalizers and the evaluation
% (steps 8-9) call the study's modules, so the demo equalizes exactly as main_compare.m:
%   1  transmitter: 16QAM symbols, RRC pulse shaping, IF up-conversion, AWG (Vpp)
%   2  central office: driver amplifier, Mach-Zehnder modulator
%   3  15 km standard single-mode fiber
%   4  remote antenna unit: optical heterodyne on the photodiode (+ PD noise),
%      RF amplifier (AGC + PA compression), RF response
%   5  1 m wireless hop at 60 GHz (+ LNA noise at the user equipment)
%   6  user equipment: square-law envelope detector, ADC
%   7  offline DSP: down-conversion, matched filter, symbol timing, gain -> symbols
%   8  equalization: LMS FFE (core/run_lms.m) and neural equalizers
%      (core/equalizer_defs.m, core/run_network.m -> equalizer.py, Python)
%   9  evaluation: EVM and BER of every equalizer (core/evaluate_outputs.m)
%  10  figures in results/: demo_link.png (the link), demo_equalizers.png (constellations)
% Run section by section (Ctrl+Enter) to inspect each stage. With the same settings and
% seed, steps 1-7 give the same received symbols as link/transmit_receive.m.
% Needs core/ and equalizer.py (Python 3 with numpy and scipy, through pyenv).

clear; close all;
here = fileparts(mfilename('fullpath'));
addpath(fullfile(here, 'core'));

%% 0  Settings
vpp_mv  = 300;                      % AWG peak-to-peak drive (mV)
rop_dbm = -11.75;                   % received optical power at the PD (model, dBm)
seed    = 2017;
n_sym   = 50000;                    % as in the study; the first 10 % train the equalizers

% equalizers (step 8): LMS settings of core/experiment_setup.m, networks by their key in
% core/equalizer_defs.m: 'liu' (replicated complex ANN), 'own_sigmoid', 'own_tanh',
% 'own_multilevel', 'own_tanh_6', 'own_multilevel_10' (our hardware candidates)
eq_keys    = {'liu', 'own_tanh_6', 'own_multilevel_10'};
lms_taps   = 10;                    % LMS taps; the decision is at the center tap
lms_mu     = 1e-3;
lms_epochs = 20;

% transmitter and scope
M = 16;  rs = 0.4e9;  fs = 20e9;    % 16QAM, 0.4 GBd, simulated at the scope's 20 GS/s
f_if = 0.8e9;  rolloff = 0.75;  rrc_span = 16;
% central office (driver gain fitted to the paper's no-EQ curves; * = assumption)
drv_gain = 24;  drv_vsat = 3;  drv_smooth = 2;          % * Rapp saturation (V)
vpi = 4.4;  vbias = vpi/2;                              % MZM, quadrature bias
% fiber
fiber_m = 15e3;  D = 17e-6;  lambda_sig = 1552.90e-9;  lambda_lo = 1552.42e-9;
% remote antenna unit
rop_ref_dbm = -10;  pd_cnr_db = 37;                     % * lumped RIN/ASE/shot/thermal
pa_asat = 1.3;  pa_smooth = 2;                          % * PA saturation / carrier
% wireless hop and UE
c0 = 299792458;
f_rf = c0/lambda_lo - c0/lambda_sig;                    % ~59.7 GHz beat
dist_m = 1;  g_tx_dbi = 20;  g_rx_dbi = 20;  p_tx_dbm = -11;  nf_db = 7;
det_bw = 1.5e9;  adc_bits = 8;

% optional impairments (all off: the reference link)
bias_drift = 0;                     % MZM bias offset (x Vpi), e.g. 0.1
rin_dbhz   = -inf;                  % laser RIN per laser (dB/Hz), e.g. -130
pd_sat     = inf;                   % PD saturation level (x carrier), e.g. 1
pa_memory  = 0;                     % PA memory polynomial strength, e.g. 1
ripple_rho = 0;                     % RAU echo amplitude at 1 ns, e.g. 0.2
adc_clip   = inf;                   % ADC full scale (x std), e.g. 1.2

sps = fs / rs;                      % 50 samples per symbol
rrc = rcosdesign(rolloff, rrc_span, sps, 'sqrt');
n_train = round(0.1 * n_sym);       % symbols known to the receiver (timing, gain)
a0 = abs((1 + exp(1j*pi*vbias/vpi)) / 2);              % unmodulated carrier amplitude
cnr_bw = rs;                        % CNRs are defined in the symbol-rate band
rapp = @(a, asat, s) a ./ max(abs(a), eps) .* ...       % Rapp AM/AM on a complex envelope
    (abs(a) ./ (1 + (abs(a)/asat).^(2*s)).^(1/(2*s)));
noise = @(n, cnr_db) sqrt(a0^2 / 10^(cnr_db/10) * fs/cnr_bw / 2) * ...  % AWGN at cnr_db
    complex(randn(n, 1), randn(n, 1));                  % below the carrier, in cnr_bw
rng(seed);

%% 1  Transmitter (AWG)
guard = rrc_span;                                       % symbols dropped at each end
idx = randi([0 M-1], n_sym + 2*guard, 1);               % 4 bits per symbol
d = qammod(idx, M, 'gray', 'UnitAveragePower', true);
s = upfirdn(d, rrc, sps);                               % RRC-shaped baseband
t = (0:numel(s)-1).' / fs;
v = real(s .* exp(1j*2*pi*f_if*t));                     % up-converted to the 0.8 GHz IF
v = v * (vpp_mv*1e-3 / (max(v) - min(v)));              % AWG output at the set Vpp (V)

%% 2  Central office: driver amplifier and MZM
g = drv_gain * v;                                       % driver: gain, Rapp saturation
vd = g ./ (1 + abs(g/drv_vsat).^(2*drv_smooth)).^(1/(2*drv_smooth));
vb = vbias + bias_drift*vpi;
E = (1 + exp(1j*pi*(vb + vd)/vpi)) / 2;                 % single-arm MZM: optical field

%% 3  Fiber: chromatic dispersion
beta2 = -D * lambda_sig^2 / (2*pi*c0);
f = (0:numel(E)-1).' / numel(E) * fs;
f(f >= fs/2) = f(f >= fs/2) - fs;                       % FFT bin frequencies
E = ifft(fft(E) .* exp(1j*beta2/2*(2*pi*f).^2*fiber_m));

%% 4  Remote antenna unit
% photodiode: beating with the second laser gives a 60 GHz current whose complex
% envelope follows E, its amplitude proportional to the received optical power
A = 10^((rop_dbm - rop_ref_dbm)/10) * E;
if isfinite(rin_dbhz)                                   % each laser's intensity noise
    sigma = sqrt(10^(rin_dbhz/10) * fs/2);
    A = A .* (1 + sigma*(randn(size(A)) + randn(size(A)))/2);
end
if isfinite(pd_sat), A = rapp(A, pd_sat*a0, 2); end     % PD saturation
A_pd = A + noise(numel(A), pd_cnr_db);                  % PD noise, CNR fixed at rop_ref
% RF amplifier: AGC back to the carrier reference, then PA compression
A_in = A_pd / 10^((rop_dbm - rop_ref_dbm)/10);
A_pa = rapp(A_in, pa_asat*a0, pa_smooth);
if pa_memory > 0                                        % PA memory polynomial
    Dm = round(0.6e-9 * fs);  u = A_in / a0;
    b_lin = pa_memory*[-0.08 0.03];  b_cub = pa_memory*[-0.05 0.02];
    for m = 1:2
        um = [zeros(m*Dm, 1); u(1:end - m*Dm)];
        A_pa = A_pa + a0*(b_lin(m)*um + b_cub(m)*um.*abs(um).^2);
    end
end
A = A_pa;
if ripple_rho > 0                                       % RF response ripple (echo)
    Dr = round(1e-9 * fs);
    A = A + ripple_rho*[zeros(Dr, 1); A(1:end - Dr)];
end

%% 5  Wireless hop: path loss, antenna gains, LNA noise at the UE
fspl_db = 20*log10(4*pi*dist_m*f_rf/c0);
noise_dbm = 10*log10(1.380649e-23*290*1e3) + 10*log10(cnr_bw) + nf_db;
cnr_ue = p_tx_dbm + g_tx_dbi + g_rx_dbi - fspl_db - noise_dbm;
A = A + noise(numel(A), cnr_ue);                        % the LNA restores the level

%% 6  User equipment: envelope detector and ADC
r = abs(A).^2;                                          % square law: back to the IF
alpha = 1 - exp(-2*pi*det_bw/fs);                       % 1st-order low-pass, 1.5 GHz
r = filter(alpha, [1, alpha - 1], r, (1 - alpha)*r(1));
if isfinite(adc_clip)                                   % limited ADC range
    r = min(max(r, mean(r) - adc_clip*std(r)), mean(r) + adc_clip*std(r));
end
lo = min(r);  step = (max(r) - lo) / (2^adc_bits - 1);  % 8-bit scope over the range
r = lo + step*round((r - lo)/step);

%% 7  Offline DSP: received symbols
z = conv(2*(r - mean(r)) .* exp(-1j*2*pi*f_if*t), rrc(:));     % down-convert, matched filter
train = guard + (1:n_train).';
nominal = rrc_span*sps + 1;                             % delay of the RRC pair
best = -inf;
for o = nominal : nominal + sps                         % timing: best training correlation
    zs = z(o + (train - 1)*sps);
    c = abs(zs' * d(train)) / norm(zs);
    if c > best, best = c; offset = o; end
end
x = z(offset + (0:numel(d)-1).'*sps);                   % one sample per symbol
gain = (x(train)' * d(train)) / (x(train)' * x(train)); % complex gain, least squares
keep = guard + (1:n_sym);
x = gain * x(keep);  d = d(keep);  idx = idx(keep);     % received and sent symbols

fprintf('Link at %d mV, ROP %.2f dBm (%d symbols, seed %d)\n', vpp_mv, rop_dbm, n_sym, seed);
fprintf('  AWG %.0f mVpp -> driver %.2f Vpp (MZM Vpi %.1f V)\n', 1e3*(max(v) - min(v)), ...
    max(vd) - min(vd), vpi);
fprintf('  PD CNR %.1f dB (%.0f dB at %.0f dBm, 2 dB per dB of ROP)\n', ...
    pd_cnr_db + 2*(rop_dbm - rop_ref_dbm), pd_cnr_db, rop_ref_dbm);
fprintf('  wireless: %.1f GHz, FSPL %.1f dB over %g m, UE CNR %.1f dB\n', ...
    f_rf/1e9, fspl_db, dist_m, cnr_ue);

%% 8  Equalization
% The modules take sequences as n_sym x n_runs x n_cond arrays: here one run, one
% condition. Every equalizer trains on the first n_train symbols (from tap index
% lms_taps on) and is tested on the rest; its output estimates the symbol at the LMS
% center tap, d(k - tau).
cfg = struct('M', M, 'n_sym', n_sym, 'train_frac', n_train/n_sym, 'taps', lms_taps, ...
    'lms_mu', lms_mu, 'lms_epochs', lms_epochs, 'seed', seed);
X = x;  D = d;  IDX = idx;
tau = floor(cfg.taps / 2);
ks_test = cfg.taps + n_train : n_sym;                   % test symbols, as core/run_lms.m

Y_lms = run_lms(cfg, X, D);                             % LMS FFE (MATLAB)
nets = equalizer_defs(eq_keys);                         % network settings
Y_nets = cell(1, numel(nets));
for i = 1:numel(nets)                                   % neural equalizers (equalizer.py)
    t0 = tic;
    Y_nets{i} = run_network(nets{i}, cfg, X, D);
    fprintf('  %s: %.1f s\n', nets{i}.name, toc(t0));
end
outs  = [{X(ks_test - tau)}, {Y_lms}, Y_nets];          % no EQ: the received symbol itself
names = [{'w/o EQ', 'LMS FFE'}, cellfun(@(n) n.name, nets, 'UniformOutput', false)];

%% 9  Evaluation: EVM and BER of every equalizer on the test symbols
[evm, ber] = evaluate_outputs(cfg, outs, D, IDX);       % hard decisions, Gray bits
n_bits = numel(ks_test) * log2(M);
fprintf('\nTest symbols: %d (%d bits)\n', numel(ks_test), n_bits);
fprintf('  %-34s %8s %10s\n', 'equalizer', 'EVM %', 'BER');
for m = 1:numel(names)
    fprintf('  %-34s %8.1f %10.2e\n', names{m}, 100*evm(m), ber(m));
end

%% 10  Figures
out_dir = here;
if isfolder(fullfile(here, 'results')), out_dir = fullfile(here, 'results'); end
fig = figure('Position', [50 50 1350 800]);
tl = tiledlayout(fig, 2, 3, 'TileSpacing', 'compact');
show = 1:min(5000, n_sym);
k = 1:50:numel(vd);

ax = nexttile(tl);                                      % sent symbols
plot(ax, real(d(show)), imag(d(show)), 'k.', 'MarkerSize', 8);
axis(ax, 'square');  axis(ax, 1.5*[-1 1 -1 1]);  grid(ax, 'on');
title(ax, '1  Transmitted 16QAM symbols');

ax = nexttile(tl);  hold(ax, 'on');                     % AWG and driver waveforms
win = round(200e-9*fs) + (1:round(20e-9*fs));           % 20 ns
yyaxis(ax, 'left');   plot(ax, 1e9*t(win), 1e3*v(win));  ylabel(ax, 'AWG (mV)');
yyaxis(ax, 'right');  plot(ax, 1e9*t(win), vd(win));     ylabel(ax, 'driver output (V)');
xlabel(ax, 'time (ns)');  grid(ax, 'on');  box(ax, 'on');
title(ax, '1-2  IF waveform: AWG and driver');

ax = nexttile(tl);  hold(ax, 'on');                     % MZM transfer
vv = linspace(-vpi, vpi, 400);
plot(ax, vv, abs((1 + exp(1j*pi*(vb + vv)/vpi))/2).^2, 'k-', 'DisplayName', 'transfer |E|^2');
plot(ax, vd(k), abs((1 + exp(1j*pi*(vb + vd(k))/vpi))/2).^2, 'r.', ...
    'DisplayName', 'operating samples');
xlabel(ax, 'drive at the MZM (V)');  ylabel(ax, 'optical intensity (norm.)');
grid(ax, 'on');  box(ax, 'on');  legend(ax, 'Location', 'northeast');
title(ax, '2  MZM: sine compression');

ax = nexttile(tl);  hold(ax, 'on');                     % RF amplifier AM/AM
plot(ax, abs(A_in(k))/a0, abs(A_pa(k))/a0, 'b.', 'MarkerSize', 3);
plot(ax, [0 max(abs(A_in(k)))/a0], [0 max(abs(A_in(k)))/a0], 'k:');
xlabel(ax, '|input| / carrier');  ylabel(ax, '|output| / carrier');
grid(ax, 'on');  box(ax, 'on');  axis(ax, 'square');
title(ax, '4  RAU amplifier AM/AM (with PD noise)');

ax = nexttile(tl);  hold(ax, 'on');                     % spectra
[p_v, fw] = pwelch(v - mean(v), 4096, [], [], fs);
p_r = pwelch(r - mean(r), 4096, [], [], fs);
plot(ax, fw/1e9, 10*log10(p_v / max(p_v)), 'DisplayName', 'AWG output');
plot(ax, fw/1e9, 10*log10(p_r / max(p_r)), 'DisplayName', 'received after ADC');
xlim(ax, [0 4]);  ylim(ax, [-70 5]);
xlabel(ax, 'frequency (GHz)');  ylabel(ax, 'PSD (dB, norm.)');
grid(ax, 'on');  box(ax, 'on');  legend(ax, 'Location', 'northeast');
title(ax, '1 vs 6  IF spectrum: distortion and noise');

ax = nexttile(tl);                                      % received symbols
plot(ax, real(x(show)), imag(x(show)), 'b.', 'MarkerSize', 3);
axis(ax, 'square');  axis(ax, 1.5*[-1 1 -1 1]);  grid(ax, 'on');
title(ax, sprintf('7  Received, no EQ: EVM %.1f%%, BER %.1e', 100*evm(1), ber(1)));

title(tl, sprintf('60-GHz A-RoF link, %d mV, %.2f dBm', vpp_mv, rop_dbm));
exportgraphics(fig, fullfile(out_dir, 'demo_link.png'), 'Resolution', 150);

% equalized constellations: every equalizer's output on the test symbols
fig = figure('Position', [50 50 280*numel(names) 330]);
tl = tiledlayout(fig, 1, numel(names), 'TileSpacing', 'compact');
for m = 1:numel(names)
    ax = nexttile(tl);
    y = outs{m}(1:min(5000, numel(ks_test)));
    plot(ax, real(y), imag(y), '.', 'MarkerSize', 2);
    axis(ax, 'square');  axis(ax, 1.5*[-1 1 -1 1]);  grid(ax, 'on');
    title(ax, {names{m}, sprintf('EVM %.1f%%, BER %.1e', 100*evm(m), ber(m))}, 'FontSize', 9);
end
title(tl, sprintf('8-9  Equalized test symbols, %d mV, %.2f dBm', vpp_mv, rop_dbm));
exportgraphics(fig, fullfile(out_dir, 'demo_equalizers.png'), 'Resolution', 150);
