// SPDX-License-Identifier: GPL-3.0-or-later
// Load the actual .dylib through OnAir's C ABI; no decoder is linked into this host.
#include "onair_plugin.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
using Cell=std::variant<int64_t,double,std::string>;
struct Table { std::vector<std::string> columns; std::vector<int> types; std::vector<std::vector<Cell>> rows; size_t total=0; };
struct Host {
    OnAirHostApi api{};
    std::map<std::string,Table> tables;
    std::map<std::string,double> numbers;
    std::map<std::string,std::string> texts;
    std::map<std::string,int> flags;
    int errors=0,events=0;
    Host() {
        api.struct_size=sizeof(api);api.abi_version=1;api.ctx=this;
        api.log=[](void* p,int level,const char*){if(level>=3)++static_cast<Host*>(p)->errors;};
        api.get_setting_text=[](void* p,const char* key,char* out,uint32_t cap){auto& s=static_cast<Host*>(p)->texts[key];if(!out||!cap)return -1;std::strncpy(out,s.c_str(),cap-1);out[cap-1]=0;return 0;};
        api.get_setting_number=[](void* p,const char* key,double def){auto& n=static_cast<Host*>(p)->numbers;auto i=n.find(key);return i==n.end()?def:i->second;};
        api.get_setting_flag=[](void* p,const char* key,int def){auto& n=static_cast<Host*>(p)->flags;auto i=n.find(key);return i==n.end()?def:i->second;};
        api.request_iq=[](void*,double,double){return -1;};
        api.table_define=[](void* p,const char* id,const char*,uint32_t n,const OnAirColumn* cols){if(n>24)return -1;auto& t=static_cast<Host*>(p)->tables[id];t.columns.clear();t.types.clear();for(uint32_t i=0;i<n;++i){t.columns.emplace_back(cols[i].name);t.types.push_back(cols[i].type);}return 0;};
        api.table_row=[](void* p,const char* id,uint32_t n,const OnAirCell* cells){auto& t=static_cast<Host*>(p)->tables[id];if(n!=t.columns.size())return -1;std::vector<Cell> row;for(uint32_t i=0;i<n;++i){auto& c=cells[i];if(t.types[i]==0)row.emplace_back(std::string(c.s?c.s:""));else if(t.types[i]==1||t.types[i]==5)row.emplace_back(c.i);else row.emplace_back(c.d);}t.rows.push_back(std::move(row));++t.total;if(t.rows.size()>2000)t.rows.erase(t.rows.begin());return 0;};
        api.table_clear=[](void* p,const char* id){auto& t=static_cast<Host*>(p)->tables[id];t.rows.clear();t.total=0;return 0;};
        api.status=[](void* p,const char*,const char* text,int lamp){if(lamp==3){++static_cast<Host*>(p)->errors;std::cerr<<"Plugin error: "<<text<<'\n';}return 0;};
        api.result_log=[](void*,const char*){return 0;};
        api.emit_event=[](void* p,const char*,uint32_t,const OnAirField*){++static_cast<Host*>(p)->events;return 0;};
    }
};
std::string escaped(const std::string& s){std::ostringstream out;for(unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else out<<c;}return out.str();}
void save(const Host& host,const std::string& path) {
    if(path.empty())return;std::ofstream out(path);if(!out)throw std::runtime_error("Cannot write private test results");
    out<<"{\"tables\":{";bool first=true;
    for(const auto& [id,t]:host.tables){if(!first)out<<',';first=false;out<<'"'<<escaped(id)<<"\":[";bool rowFirst=true;
        for(const auto& row:t.rows){if(!rowFirst)out<<',';rowFirst=false;out<<'{';for(size_t i=0;i<row.size();++i){if(i)out<<',';out<<'"'<<escaped(t.columns[i])<<"\":";std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string>)out<<'"'<<escaped(v)<<'"';else out<<std::setprecision(12)<<v;},row[i]);}out<<'}';}out<<']';}
    out<<"}}\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("Usage: ogn_replay LIBRARY --self-test | --input FILE --format ci16|cf32 --rate HZ --centre HZ --mode both|m1|m2 [--output JSON] [--expect-positions N] [--recording-centre-mhz MHZ] [--chunk N] [--all-fields]");
        std::string input,format="ci16",mode="both",output;double rate=1250000,centre=868300000;size_t chunk=16384;long expected=-1;bool self=false,checkPublic=false,checkMissing=false;Host host;
        for(int i=2;i<argc;++i){std::string a=argv[i];if(a=="--self-test")self=true;else if(a=="--expect-public-vector")checkPublic=true;else if(a=="--expect-unavailable")checkMissing=true;else if(a=="--all-fields")host.flags["show_field_table"]=1;else {if(i+1>=argc)throw std::runtime_error("Missing argument");std::string v=argv[++i];if(a=="--input")input=v;else if(a=="--format")format=v;else if(a=="--mode")mode=v;else if(a=="--rate")rate=std::stod(v);else if(a=="--centre")centre=std::stod(v);else if(a=="--output")output=v;else if(a=="--expect-positions")expected=std::stol(v);else if(a=="--recording-centre-mhz")host.texts["recording_center_mhz"]=v;else if(a=="--chunk")chunk=std::stoull(v);else if(a=="--max-corrected")host.numbers["max_corrected_bits"]=std::stod(v);else throw std::runtime_error("Unknown argument");}}
        if(chunk<1||chunk>131072||!std::isfinite(rate)||rate<=0)throw std::runtime_error("Invalid input parameters");
        void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());
        const auto entry=reinterpret_cast<onair_plugin_entry_fn>(dlsym(library,"onair_plugin_entry"));if(!entry)throw std::runtime_error("Missing plugin entry");
        if(entry(0)||entry(2))throw std::runtime_error("Plugin accepted incompatible ABI");
        const auto* vt=entry(1);if(!vt||vt->struct_size<sizeof(*vt)||std::string(vt->id)!="ogn-mband")throw std::runtime_error("Invalid plugin table");
        void* instance=vt->create(&host.api,mode.c_str());if(!instance)throw std::runtime_error("Plugin create failed");
        vt->configure(instance,centre,rate);uint64_t index=0;double maxMs=0;
        auto feed=[&](const std::vector<float>& iq,size_t n){OnAirIqMeta meta{sizeof(meta),centre,rate,double(index)/rate,index};auto begin=std::chrono::steady_clock::now();vt->process_iq(instance,iq.data(),uint32_t(n),&meta);maxMs=std::max(maxMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());index+=n;};
        if(self){std::vector<float> quiet(32768,0);feed(quiet,quiet.size()/2);vt->tick(instance,1);}
        else {
            std::ifstream file(input,std::ios::binary);if(!file)throw std::runtime_error("Cannot open IQ recording");
            const size_t bytesPerSample=format=="ci16"?4:format=="cf32"?8:0;if(!bytesPerSample)throw std::runtime_error("Unsupported format");
            std::vector<uint8_t> bytes(chunk*bytesPerSample);std::vector<float> iq(chunk*2);
            while(file){file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));const size_t got=size_t(file.gcount());if(got%bytesPerSample)throw std::runtime_error("Truncated IQ sample");const size_t n=got/bytesPerSample;if(!n)break;
                for(size_t i=0;i<2*n;++i){if(format=="ci16"){const uint16_t raw=uint16_t(bytes[2*i])|(uint16_t(bytes[2*i+1])<<8);iq[i]=float(raw>=32768?int(raw)-65536:int(raw))/32768.f;}else {uint32_t bits=0;for(int b=0;b<4;++b)bits|=uint32_t(bytes[4*i+b])<<(8*b);std::memcpy(&iq[i],&bits,4);}}
                feed(iq,n);
            }
        }
        vt->destroy(instance);dlclose(library);
        if(host.errors)throw std::runtime_error("Plugin reported decoder errors");
        if(host.events)throw std::runtime_error("Privacy default emitted automation events");
        const size_t positions=host.tables["positions"].total;
        if(self&&positions)throw std::runtime_error("Quiet input produced a position");
        if(expected>=0&&positions!=size_t(expected))throw std::runtime_error("Unexpected position count: "+std::to_string(positions));
        if(maxMs>=2000)throw std::runtime_error("Plugin exceeded OnAir watchdog budget");
        if (checkPublic || checkMissing) {
            const auto& t=host.tables.at("positions");
            for (const auto& row:t.rows) {
                auto at=[&](const char* key)->const Cell& { auto it=std::find(t.columns.begin(),t.columns.end(),key);if(it==t.columns.end())throw std::runtime_error("Missing table column");return row[size_t(it-t.columns.begin())]; };
                if(std::get<std::string>(at("Address"))!="123456" || std::abs(std::stod(std::get<std::string>(at("Latitude deg")))-50.1234466666667)>0.000001 || std::get<double>(at("Speed m/s"))!=35.5)throw std::runtime_error("Public vector table values do not match");
                if(checkMissing)for(const char* column:{"Pressure alt m","Climb m/s","Turn deg/s"})if(std::get<std::string>(at(column))!="--")throw std::runtime_error("Absent field displayed as a numeric value");
            }
        }
        save(host,output);
        std::cout<<"Samples "<<index<<"; positions "<<positions<<"; accepted packets "<<host.tables["packets"].total<<"; automation events "<<host.events<<"; max callback "<<std::fixed<<std::setprecision(2)<<maxMs<<" ms\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
