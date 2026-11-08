// SPDX-License-Identifier: GPL-3.0-or-later
#include "ogn.hpp"
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ogn {
namespace mb = dect2::modebuilder;
mb::Definition radioDefinition() {
    mb::Definition d;
    d.name = "OGN1"; d.mod = mb::Mod::GFSK; d.order = 2; d.symbolRate = 100000;
    d.deviationHz = 50000; d.bt = 0.5; d.bandwidthHz = 300000;
    d.pulse = mb::Pulse::Gaussian; d.pulseSet = true;
    d.line = mb::Line::Manchester; d.polarity = mb::Polarity::Auto;
    d.sync = {0x0AF3656C, 32}; d.syncMaxErrors = 0; d.preamble = {0, 4};
    d.fixedBytes = 26; d.sampleRate = 1250000; d.squelchDb = 6;
    return d;
}
Decoder::Decoder(Options options, std::function<void(const Packet&)> sink) : options_(std::move(options)), sink_(std::move(sink)) {
    if (options_.mode != "m1" && options_.mode != "m2" && options_.mode != "both") throw std::runtime_error("Unknown OGN mode");
}
void Decoder::configure(double centreHz, double sampleRate) {
    if (!std::isfinite(centreHz) || !std::isfinite(sampleRate) || sampleRate < 400000 || sampleRate > 20000000)
        throw std::runtime_error("OGN requires IQ at 400 kS/s to 20 MS/s");
    centre_ = options_.centreOverrideHz > 0 ? options_.centreOverrideHz : centreHz;
    rate_ = sampleRate; started_ = false; flushed_ = false; segmentSeconds_ = 0;
    resetReceivers();
}
void Decoder::resetReceivers() {
    channels_.clear();
    for (int index = 0; index < 2; ++index) {
        if ((options_.mode == "m1" && index == 1) || (options_.mode == "m2" && index == 0)) continue;
        const double frequency = (index ? 868400000.0 : 868200000.0) + options_.trimHz;
        const double offset = frequency - centre_;
        if (std::abs(offset) + 150000 > rate_ * 0.49) throw std::runtime_error("Selected OGN channel is outside the IQ bandwidth; check the recording centre");
        auto d = radioDefinition(); d.offsetHz = offset; d.sampleRate = rate_;
        d.squelchDb = options_.squelchDb; d.syncMaxErrors = options_.syncErrors;
        Channel ch; ch.name = index ? "M2" : "M1"; ch.frequency = frequency; ch.rx = std::make_unique<mb::Receiver>(d);
        ch.rx->configure(rate_); channels_.push_back(std::move(ch));
    }
}
void Decoder::collect(Channel& channel) {
    for (const auto& frame : channel.rx->take()) {
        ++counts_.candidates;
        if (frame.bytes.size() != 26) { ++counts_.rejected; continue; }
        Codeword raw{}; std::copy(frame.bytes.begin(), frame.bytes.end(), raw.begin());
        Packet p;
        if (!decode(raw, p, options_.maxCorrections)) { ++counts_.rejected; continue; }
        ++counts_.accepted; if (p.ldpc.corrected) ++counts_.corrected;
        p.fields["channel"] = channel.name; p.fields["frequency_hz"] = channel.frequency;
        p.fields["stream_s"] = segmentSeconds_ + frame.timeSec;
        p.fields["snr_db"] = double(frame.snrDb); p.fields["sync_errors"] = int64_t(frame.syncErrors);
        sink_(p);
    }
}
void Decoder::feed(const float* iq, size_t n, uint64_t firstSample) {
    if (rate_ <= 0 || (!iq && n)) throw std::runtime_error("OGN stream is not configured");
    if (!n) return;
    if (!started_) { started_ = true; origin_ = firstSample; expected_ = firstSample; }
    if (firstSample != expected_ || flushed_) {
        segmentSeconds_ = firstSample >= origin_ ? double(firstSample - origin_) / rate_ : 0;
        resetReceivers(); ++counts_.resets;
    }
    flushed_ = false;
    // Copy through the C ABI rather than assuming float[] aliases complex<float>.
    input_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const float a = iq[2 * i], b = iq[2 * i + 1];
        input_[i] = mb::cf(std::isfinite(a) ? a : 0, std::isfinite(b) ? b : 0);
    }
    for (auto& channel : channels_) { channel.rx->feed(input_.data(), n); collect(channel); }
    expected_ = firstSample + n;
}
void Decoder::flush() {
    if (flushed_ || !started_) return;
    for (auto& channel : channels_) { channel.rx->flush(); collect(channel); }
    flushed_ = true;
}
std::string Decoder::streamDescription() const {
    std::ostringstream s; s.setf(std::ios::fixed); s.precision(6);
    s << centre_ / 1e6 << " MHz, " << rate_ / 1e6 << " MS/s; ";
    for (const auto& c : channels_) s << c.name << " " << (c.frequency - centre_) / 1000 << " kHz; ";
    return s.str();
}
} // namespace ogn
