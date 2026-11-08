// SPDX-License-Identifier: GPL-3.0-or-later
#include "ogn.hpp"
#include "onair_plugin.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
struct Column { const char* key; const char* label; int type; };
using Columns = std::vector<Column>;
const Columns positions = {
    {"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"address_type","Address type",0},
    {"second","GNSS second",1},{"latitude_deg","Latitude deg",0},{"longitude_deg","Longitude deg",0},
    {"altitude_msl_m","GNSS MSL m",1},{"altitude_std_m","Pressure alt m",0},{"speed_mps","Speed m/s",2},
    {"track_deg","Track deg",2},{"climb_mps","Climb m/s",0},{"turn_dps","Turn deg/s",0},{"dop","DOP",2},
    {"fix_quality","Fix quality",1},{"fix_dimensions","Fix dimensions",1},{"aircraft_type","Aircraft type",0},
    {"relay","Relay",1},{"stealth","Stealth",5},{"emergency","Emergency",5},{"corrected_bits","LDPC corrected bits",1},
    {"ldpc_failed_before","Checks failed before",1},{"snr_db","SNR dB",2},{"position_valid","Position valid",5}};
const Columns packets = {
    {"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"address_type","Address type",0},
    {"kind","Packet type",0},{"relay","Relay",1},{"non_position","Non-position",5},{"emergency","Emergency",5},
    {"encrypted_custom","Custom/encrypted",5},{"corrected_bits","LDPC corrected bits",1},
    {"ldpc_failed_before","Checks failed before",1},{"sync_errors","Sync errors",1},{"snr_db","SNR dB",2}};
const Columns statusColumns = {
    {"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"second","GNSS second",1},
    {"voltage_v","Battery V",2},{"temperature_c","Temperature C",0},{"humidity_pct","Humidity %",0},
    {"pressure_hpa","Pressure hPa",0},{"altitude_msl_m","GNSS MSL m",0},{"fix_quality","Fix quality",1},
    {"satellites","Satellites",1},{"gps_snr_db","GPS SNR dB",1},{"tx_power_dbm","TX power dBm",1},
    {"radio_noise_dbm","Radio noise dBm",2},{"rx_packets_per_min","RX packets/min",1},{"firmware","Firmware",1},
    {"hardware","Hardware",1},{"audio_noise_db","Audio noise dB",1},{"pulse_bpm","Pulse bpm",0},{"oxygen_pct","Oxygen %",0},
    {"corrected_bits","LDPC corrected bits",1}};
const Columns infoColumns = {
    {"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"info_check_ok","Info checksum",5},
    {"registration","Registration",0},{"model","Model",0},{"type","Type",0},{"pilot","Pilot",0},
    {"manufacturer","Manufacturer",0},{"serial","Serial",0},{"id","Info ID",0},{"class","Class",0},
    {"task","Task",0},{"base","Base",0},{"ice","ICE",0},{"pilot_id","Pilot ID",0},
    {"hardware","Hardware",0},{"software","Software",0},{"crew","Crew",0}};
const Columns windColumns = {
    {"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"second","Second",1},
    {"latitude_deg","Latitude deg",0},{"longitude_deg","Longitude deg",0},{"altitude_msl_m","MSL m",1},
    {"altitude_std_m","Pressure alt m",0},{"speed_mps","Wind m/s",2},{"track_deg","Wind direction deg",2},
    {"climb_mps","Vertical wind m/s",0},{"corrected_bits","LDPC corrected bits",1}};

struct State {
    const OnAirHostApi* host;
    std::string mode;
    std::unique_ptr<ogn::Decoder> decoder;
    double centre = 0, rate = 0;
    bool emit = false, fields = false, hasData = false;
    Clock::time_point lastData{}, eventSecond{};
    int eventCount = 0;
    std::set<std::string> tables;
    State(const OnAirHostApi* h, std::string m) : host(h), mode(std::move(m)) {}
    void status(const char* key, const std::string& text, int lamp = ONAIR_LAMP_OK) {
        host->status(host->ctx, key, text.c_str(), lamp);
    }
    void ensureTable(const char* id, const char* title, const Columns& cols) {
        if (!tables.insert(id).second) return;
        std::vector<OnAirColumn> columns; for (const auto& c : cols) columns.push_back({c.label,c.type});
        if (host->table_define(host->ctx, id, title, uint32_t(columns.size()), columns.data()) < 0)
            throw std::runtime_error("OnAir refused an OGN result table");
    }
    void row(const char* id, const char* title, const Columns& cols, const ogn::Fields& f) {
        ensureTable(id,title,cols);
        std::vector<OnAirCell> cells; std::vector<std::string> text; text.reserve(cols.size());
        for (const auto& c : cols) {
            OnAirCell cell{}; cell.type = c.type;
            const auto it = f.find(c.key);
            if (it == f.end()) {
                if (c.type != ONAIR_COL_TEXT) throw std::runtime_error("Missing required OGN table value");
                cell.s = "--";
            } else if (c.type == ONAIR_COL_TEXT) {
                if (std::string(c.key) == "latitude_deg" || std::string(c.key) == "longitude_deg") {
                    std::ostringstream out; out << std::fixed << std::setprecision(6) << std::get<double>(it->second);
                    text.push_back(out.str());
                } else text.push_back(ogn::textValue(it->second));
                cell.s = text.back().c_str();
            } else std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::string>) throw std::runtime_error("Wrong OGN numeric table type");
                else if (c.type == ONAIR_COL_FLOAT) cell.d = double(value);
                else cell.i = int64_t(value);
            }, it->second);
            cells.push_back(cell);
        }
        host->table_row(host->ctx, id, uint32_t(cells.size()), cells.data());
    }
    void publish(const ogn::Packet& packet) {
        const auto& f = packet.fields;
        if (packet.kind == "position") row("positions","OGN positions (LDPC and header checked)",positions,f);
        else if (packet.kind == "status") row("status_packets","OGN device status",statusColumns,f);
        else if (packet.kind == "info") row("info","OGN information",infoColumns,f);
        else if (packet.kind == "wind") row("wind","OGN wind reports",windColumns,f);
        row("packets","Accepted OGN packets",packets,f);
        if (fields) {
            const Columns c{{"stream_s","Stream s",2},{"channel","Channel",0},{"address","Address",0},{"field","Field",0},{"value","Value",0}};
            for (const auto& [key,value] : f) {
                ogn::Fields item{{"stream_s",f.at("stream_s")},{"channel",f.at("channel")},{"address",f.at("address")},{"field",key},{"value",ogn::textValue(value)}};
                row("fields","All decoded fields",c,item);
            }
        }
        if (emit && host->emit_event) {
            const auto now = Clock::now();
            if (now - eventSecond > std::chrono::seconds(1)) { eventSecond = now; eventCount = 0; }
            if (eventCount++ < 50) {
                std::vector<OnAirField> event;
                for (const auto& [key,value] : f) {
                    if (event.size() >= 32) break;
                    OnAirField item{}; item.key = key.c_str();
                    std::visit([&](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) { item.is_text = 1; item.s = v.c_str(); }
                        else item.d = double(v);
                    },value);
                    event.push_back(item);
                }
                host->emit_event(host->ctx,"ogn.packet",uint32_t(event.size()),event.data());
            }
        }
    }
    void report() {
        if (!decoder) return;
        const auto c = decoder->counters();
        status("Reception",decoder->streamDescription());
        status("Packets",std::to_string(c.accepted)+" accepted; "+std::to_string(c.candidates)+" sync candidates; "+std::to_string(c.rejected)+" rejected; "+std::to_string(c.corrected)+" corrected",c.accepted ? 1 : 0);
        status("Continuity",std::to_string(c.resets)+" stream gaps",c.resets ? 2 : 1);
        status("Privacy",emit ? "Local automation events enabled" : "Local display only; no network or automatic file output");
    }
    void rebuild(bool clear) {
        ogn::Options o; o.mode = mode;
        char text[128]{}; host->get_setting_text(host->ctx,"recording_center_mhz",text,sizeof text);
        if (text[0]) {
            char* end = nullptr; const double value = std::strtod(text,&end);
            while (end && *end == ' ') ++end;
            if (!end || *end || !std::isfinite(value) || value < 800 || value > 950)
                throw std::runtime_error("Recording centre must be a MHz value, or blank");
            o.centreOverrideHz = value * 1e6;
        }
        o.trimHz = host->get_setting_number(host->ctx,"frequency_trim_hz",0);
        o.squelchDb = host->get_setting_number(host->ctx,"squelch_db",6);
        const double sync = host->get_setting_number(host->ctx,"sync_errors",0);
        const double corrections = host->get_setting_number(host->ctx,"max_corrected_bits",2);
        if (!std::isfinite(sync) || !std::isfinite(corrections)) throw std::runtime_error("Invalid OGN correction setting");
        o.syncErrors = int(std::clamp(sync,0.0,2.0));
        o.maxCorrections = int(std::clamp(corrections,0.0,6.0));
        if (!std::isfinite(o.trimHz) || !std::isfinite(o.squelchDb)) throw std::runtime_error("Invalid OGN setting");
        o.trimHz = std::clamp(o.trimHz,-15000.0,15000.0); o.squelchDb = std::clamp(o.squelchDb,3.0,15.0);
        emit = host->get_setting_flag(host->ctx,"emit_events",0) != 0;
        fields = host->get_setting_flag(host->ctx,"show_field_table",0) != 0;
        auto next = std::make_unique<ogn::Decoder>(o,[this](const ogn::Packet& p){publish(p);});
        if (rate > 0) next->configure(centre,rate);
        decoder = std::move(next); hasData = false;
        if (clear) for (const auto& id : tables) host->table_clear(host->ctx,id.c_str());
        status("Decoder","OGN1 M-band 0.1.0; 100 kchip/s GFSK, Manchester, LDPC, TEA dewhitening");
        report();
    }
};
void error(State* s) noexcept {
    try { throw; } catch (const std::exception& e) { s->status("Decoder",e.what(),3); }
    catch (...) { s->status("Decoder","Unexpected decoder error",3); }
}
void* create(const OnAirHostApi* host,const char* mode) noexcept {
    if (!host || host->abi_version != 1 || host->struct_size < sizeof(OnAirHostApi) || !mode || !host->status || !host->table_define || !host->table_row || !host->table_clear || !host->get_setting_text || !host->get_setting_number || !host->get_setting_flag) return nullptr;
    try { auto s = std::make_unique<State>(host,mode); s->rebuild(false); return s.release(); } catch (...) { return nullptr; }
}
void destroy(void* p) noexcept { auto* s = static_cast<State*>(p); if (!s) return; try { if(s->decoder)s->decoder->flush(); } catch (...) { error(s); } delete s; }
void configure(void* p,double centre,double rate) noexcept { auto* s=static_cast<State*>(p); try { s->centre=centre; s->rate=rate; s->rebuild(false); } catch (...) { s->decoder.reset(); error(s); } }
void process(void* p,const float* iq,uint32_t n,const OnAirIqMeta* meta) noexcept {
    auto* s=static_cast<State*>(p);
    try {
        if (!iq && n) throw std::runtime_error("Missing IQ samples");
        if (!meta || meta->struct_size < sizeof(OnAirIqMeta)) throw std::runtime_error("Missing IQ metadata");
        if (!s->decoder || s->rate != meta->sample_rate || s->centre != meta->centre_hz) {
            s->rate=meta->sample_rate; s->centre=meta->centre_hz; s->rebuild(false);
        }
        // Bound scratch memory and CPU even if a host passes an unusually large block.
        for (uint32_t offset=0; offset<n; offset+=std::min<uint32_t>(16384,n-offset)) {
            const uint32_t count=std::min<uint32_t>(16384,n-offset);
            s->decoder->feed(iq+2*size_t(offset),count,meta->first_sample_index+offset);
        }
        s->lastData=Clock::now(); s->hasData=true; s->report();
    } catch (...) { error(s); }
}
void tick(void* p,double) noexcept { auto* s=static_cast<State*>(p); try { if(s->hasData && Clock::now()-s->lastData>std::chrono::milliseconds(250)) { if(s->decoder)s->decoder->flush(); s->report(); } } catch (...) { error(s); } }
void setting(void* p,const char*) noexcept { auto* s=static_cast<State*>(p); try { s->rebuild(true); } catch (...) { s->decoder.reset(); error(s); } }
const OnAirPluginVTable vtable{sizeof(OnAirPluginVTable),1,ONAIR_CAP_IQ_SINK|ONAIR_CAP_TICK|ONAIR_CAP_SETTINGS,"ogn-mband",create,destroy,configure,process,nullptr,tick,setting};
}
extern "C" ONAIR_EXPORT const OnAirPluginVTable* onair_plugin_entry(uint32_t abi) { return abi == 1 ? &vtable : nullptr; }
