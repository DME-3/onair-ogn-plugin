// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dect2/modebuilder/modebuilder.h"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace ogn {
using Codeword = std::array<uint8_t, 26>;
using Value = std::variant<int64_t, double, bool, std::string>;
using Fields = std::map<std::string, Value>;
struct LdpcResult { bool valid = false; int failedBefore = 0, corrected = 0, iterations = 0; };
int parityFailures(const Codeword& word);
LdpcResult correct(Codeword& word, int maxChanges = 2);
void encodeParity(Codeword& word);
void whiten(Codeword& word, bool inverse);
uint32_t read32(const uint8_t* p);
void write32(uint8_t* p, uint32_t x);
int unsignedVr(unsigned x, unsigned bits);
int signedVr(unsigned x, unsigned bits);
struct Packet {
    Fields fields;
    std::string kind;
    Codeword corrected{}, plain{};
    LdpcResult ldpc;
};
bool decode(const Codeword& raw, Packet& packet, int maxChanges = 2);
std::string jsonFields(const Fields& fields);
std::string textValue(const Value& value);

struct Options {
    std::string mode = "both";
    double centreOverrideHz = 0, trimHz = 0, squelchDb = 6;
    int syncErrors = 0, maxCorrections = 2;
};
struct Counters { uint64_t candidates = 0, accepted = 0, rejected = 0, corrected = 0, resets = 0; };
class Decoder {
public:
    explicit Decoder(Options options, std::function<void(const Packet&)> sink);
    void configure(double centreHz, double sampleRate);
    void feed(const float* interleavedIq, size_t n, uint64_t firstSample);
    void flush();
    Counters counters() const { return counts_; }
    std::string streamDescription() const;
private:
    struct Channel { std::string name; double frequency = 0; std::unique_ptr<dect2::modebuilder::Receiver> rx; };
    Options options_;
    std::function<void(const Packet&)> sink_;
    std::vector<Channel> channels_;
    std::vector<dect2::modebuilder::cf> input_;
    Counters counts_;
    double centre_ = 0, rate_ = 0, segmentSeconds_ = 0;
    uint64_t expected_ = 0, origin_ = 0;
    bool started_ = false, flushed_ = false;
    void resetReceivers();
    void collect(Channel& channel);
};
dect2::modebuilder::Definition radioDefinition();
} // namespace ogn
