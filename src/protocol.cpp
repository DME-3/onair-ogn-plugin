// SPDX-License-Identifier: GPL-3.0-or-later
// OGN1 public field layout; explicit little-endian loads avoid C++ bitfield ABI assumptions.
#include "ogn.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ogn {
uint32_t read32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void write32(uint8_t* p, uint32_t x) { for (int b = 0; b < 4; ++b) p[b] = uint8_t(x >> (b * 8)); }
int unsignedVr(unsigned x, unsigned bits) {
    const unsigned range = x >> bits, threshold = 1u << bits, mantissa = x & (threshold - 1);
    return int(((1u << range) - 1) * threshold + (mantissa << range) + (range ? 1u << (range - 1) : 0));
}
int signedVr(unsigned x, unsigned bits) {
    const unsigned sign = 1u << (bits + 2);
    const int value = unsignedVr(x & (sign - 1), bits);
    return (x & sign) ? -value : value;
}
void whiten(Codeword& word, bool inverse) {
    constexpr uint32_t delta = 0x9e3779b9u;
    for (int offset : {4, 12}) {
        uint32_t a = read32(&word[offset]), b = read32(&word[offset + 4]);
        uint32_t sum = inverse ? delta * 8u : 0;
        for (int cycle = 0; cycle < 8; ++cycle) {
            if (inverse) {
                b -= (a << 4) ^ (a + sum) ^ (a >> 5);
                a -= (b << 4) ^ (b + sum) ^ (b >> 5);
                sum -= delta;
            } else {
                sum += delta;
                a += (b << 4) ^ (b + sum) ^ (b >> 5);
                b += (a << 4) ^ (a + sum) ^ (a >> 5);
            }
        }
        write32(&word[offset], a); write32(&word[offset + 4], b);
    }
}
namespace {
std::string hex(unsigned v, int digits) { std::ostringstream s; s << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << v; return s.str(); }
int sign24(uint32_t v) { return (v & 0x800000u) ? int(v & 0xffffffu) - 0x1000000 : int(v & 0xffffffu); }
void integer(Fields& f, const char* key, unsigned x) { f[key] = int64_t(x); }
void coordinates(Fields& f, uint32_t a, uint32_t b, bool valid) {
    const double lat = (8.0 * sign24(a) + 4.0) / 600000.0;
    const double lon = (16.0 * sign24(b) + 8.0) / 600000.0;
    const bool usable = valid && std::abs(lat) <= 90 && std::abs(lon) <= 180;
    f["position_valid"] = usable;
    if (usable) { f["latitude_deg"] = lat; f["longitude_deg"] = lon; }
}
void motion(Fields& f, const uint32_t* w) {
    f["altitude_msl_m"] = int64_t(unsignedVr(w[2] & 0x3fff, 12));
    f["speed_mps"] = 0.1 * unsignedVr((w[2] >> 14) & 0x3ff, 8);
    f["track_deg"] = 0.1 * (((w[3] & 0x3ff) * 3600 + 512) >> 10);
    const unsigned climb = (w[3] >> 10) & 0x1ff;
    if (climb != 0x100) f["climb_mps"] = 0.1 * signedVr(climb, 6);
    const unsigned low = w[3] >> 24, msb = (w[1] >> 30) & 1;
    f["baro_available"] = bool(low || msb);
    if (low || msb) {
        const int difference = int(low) - (msb ? 0 : 256);
        f["baro_difference_m"] = int64_t(difference);
        f["altitude_std_m"] = int64_t(unsignedVr(w[2] & 0x3fff, 12) + difference);
    }
}
std::string escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else out << c;
    }
    return out.str();
}
}
bool decode(const Codeword& raw, Packet& p, int maxChanges) {
    p = Packet{}; p.corrected = raw;
    p.ldpc = correct(p.corrected, maxChanges);
    if (!p.ldpc.valid) return false;
    const uint32_t header = read32(p.corrected.data());
    if (__builtin_popcount(header & 0x0fffffffu) & 1) return false;
    auto& f = p.fields;
    f["address"] = hex(header & 0xffffff, 6);
    static const char* addressTypes[] = {"Random", "ICAO", "FLARM", "OGN"};
    integer(f, "address_type_id", (header >> 24) & 3);
    f["address_type"] = std::string(addressTypes[(header >> 24) & 3]);
    integer(f, "relay", (header >> 28) & 3);
    f["non_position"] = bool(header & (1u << 26));
    f["encrypted_custom"] = bool(header & (1u << 30));
    f["emergency"] = bool(header & (1u << 31));
    f["header_parity_ok"] = true;
    f["ldpc_ok"] = true;
    f["ldpc_failed_before"] = int64_t(p.ldpc.failedBefore);
    f["corrected_bits"] = int64_t(p.ldpc.corrected);
    f["ldpc_iterations"] = int64_t(p.ldpc.iterations);
    p.plain = p.corrected;
    if (header & (1u << 30)) { p.kind = "encrypted/custom"; f["kind"] = p.kind; return true; }
    whiten(p.plain, true);
    uint32_t w[4]; for (int i = 0; i < 4; ++i) w[i] = read32(&p.plain[4 + 4 * i]);
    if (!(header & (1u << 26))) {
        p.kind = "position";
        const unsigned time = (w[0] >> 24) & 63, quality = w[0] >> 30;
        integer(f, "second", time); f["time_valid"] = time < 60;
        integer(f, "fix_quality", quality); integer(f, "fix_dimensions", 2 + (w[1] >> 31));
        coordinates(f, w[0], w[1], quality != 0 && time < 60);
        motion(f, w);
        const unsigned turn = w[2] >> 24;
        if (turn != 0x80) f["turn_dps"] = 0.1 * signedVr(turn, 5);
        f["dop"] = 1.0 + 0.1 * unsignedVr((w[1] >> 24) & 63, 4);
        integer(f, "aircraft_type_id", (w[3] >> 20) & 15);
        static const char* types[] = {"Unknown", "Glider", "Tow plane", "Helicopter", "Parachute", "Drop plane", "Hang glider", "Paraglider", "Powered aircraft", "Jet", "Unknown", "Balloon", "Airship", "UAV", "Ground vehicle", "Static"};
        f["aircraft_type"] = std::string(types[(w[3] >> 20) & 15]);
        f["stealth"] = bool(w[3] & (1u << 19));
    } else {
        const unsigned report = (w[3] >> 20) & 15; integer(f, "report_type", report);
        if (report == 0) {
            p.kind = "status";
            integer(f, "second", (w[0] >> 24) & 63); integer(f, "fix_quality", w[0] >> 30);
            if (w[0] >> 30) f["altitude_msl_m"] = int64_t(unsignedVr(w[2] & 0x3fff, 12));
            integer(f, "satellites", w[2] >> 28); integer(f, "gps_snr_db", 8 + ((w[0] >> 15) & 31));
            integer(f, "rx_packets_per_min", (1u << ((w[0] >> 20) & 15)) - 1);
            integer(f, "firmware", w[3] & 255); integer(f, "hardware", (w[3] >> 8) & 255);
            integer(f, "tx_power_dbm", 4 + ((w[3] >> 16) & 15));
            f["radio_noise_dbm"] = -0.5 * ((w[1] >> 8) & 255);
            f["voltage_v"] = unsignedVr(w[3] >> 24, 6) / 64.0;
            const unsigned temp = (w[1] >> 16) & 255, humidity = w[1] >> 24;
            if (temp != 0x80) f["temperature_c"] = (200 + signedVr(temp, 5)) / 10.0;
            if (humidity != 0x80) f["humidity_pct"] = (520 + signedVr(humidity, 5)) / 10.0;
            const unsigned pressure = (w[2] >> 14) & 0x3fff;
            if (pressure) f["pressure_hpa"] = pressure * 0.08;
            integer(f, "audio_noise_db", w[1] & 255);
            if (w[0] & 255) integer(f, "pulse_bpm", w[0] & 255);
            if ((w[0] >> 8) & 127) integer(f, "oxygen_pct", (w[0] >> 8) & 127);
        } else if (report == 1) {
            p.kind = "info"; uint8_t check = 0;
            for (int i = 4; i < 19; ++i) check ^= p.plain[i];
            const bool infoOk = check == p.plain[19]; f["info_check_ok"] = infoOk;
            if (infoOk) {
                static const char* names[] = {"pilot", "manufacturer", "model", "type", "serial", "registration", "id", "class", "task", "base", "ice", "pilot_id", "hardware", "software", "crew"};
                std::string value;
                const int last = p.plain[18] & 15;
                for (int index = 0; index <= last; ++index) {
                    const unsigned shift = unsigned(index * 7), offset = shift / 8;
                    unsigned ch = unsigned(p.plain[4 + offset]) >> (shift % 8);
                    if (shift % 8 > 1) ch |= unsigned(p.plain[5 + offset]) << (8 - shift % 8);
                    ch &= 127;
                    if (ch < 32) { f[ch < 15 ? names[ch] : "info_" + std::to_string(ch)] = value; value.clear(); }
                    else value += char(ch);
                }
            }
        } else if (report == 2) {
            p.kind = "wind"; const unsigned time = (w[0] >> 24) & 63;
            integer(f, "second", time); f["time_valid"] = time < 60;
            coordinates(f, w[0], w[1], time < 60); motion(f, w);
        } else p.kind = "other report";
    }
    f["kind"] = p.kind;
    return true;
}
std::string textValue(const Value& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) return v;
        else if constexpr (std::is_same_v<T, bool>) return v ? "yes" : "no";
        else { std::ostringstream s; s << std::setprecision(10) << v; return s.str(); }
    }, value);
}
std::string jsonFields(const Fields& fields) {
    std::ostringstream out; out << '{'; bool first = true;
    for (const auto& [name, value] : fields) {
        if (!first) out << ','; first = false;
        out << '"' << escape(name) << "\":";
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) out << '"' << escape(v) << '"';
            else if constexpr (std::is_same_v<T, bool>) out << (v ? "true" : "false");
            else if constexpr (std::is_same_v<T, double>) { if (std::isfinite(v)) out << std::setprecision(12) << v; else out << "null"; }
            else out << v;
        }, value);
    }
    return out.str() + '}';
}
} // namespace ogn
