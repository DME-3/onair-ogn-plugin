// SPDX-License-Identifier: GPL-3.0-or-later
#include "ogn.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool condition, const char* why) { ++checks; if(!condition)throw std::runtime_error(why); }
double number(const ogn::Packet& p,const char* key) {
    const auto& v=p.fields.at(key); if(auto x=std::get_if<double>(&v))return *x;
    return double(std::get<int64_t>(v));
}
const ogn::Codeword reference{0x56,0x34,0x12,0x0B,0x48,0x5B,0xB7,0xE9,0xCF,0x7B,0x91,0x4D,0x78,0x31,0x3A,0xD6,0xB9,0xC5,0x00,0x07,0x3B,0x3A,0x0E,0x2F,0xA1,0x39};
ogn::Codeword pack(uint32_t h, const std::array<uint32_t,4>& data) {
    if(__builtin_popcount(h&0xfffffff)&1)h^=1u<<27;
    ogn::Codeword p{};ogn::write32(p.data(),h);
    for(int i=0;i<4;++i)ogn::write32(&p[4+4*i],data[i]);
    ogn::whiten(p,false);ogn::encodeParity(p);return p;
}
void protocolTests() {
    expect(ogn::parityFailures(reference)==0,"published reference LDPC");
    auto reencoded=reference;ogn::encodeParity(reencoded);expect(reencoded==reference,"systematic parity encoder/reference");
    auto plain=reference;ogn::whiten(plain,true);
    const uint32_t expected[]{0x0B123456,0x4F395C9A,0xC50B83C5,0x0F4C44B0,0x32D0B573};
    for(int i=0;i<5;++i)expect(ogn::read32(&plain[4*i])==expected[i],"independent public TEA plaintext vector");
    ogn::whiten(plain,false);expect(plain==reference,"TEA exact reverse preserves header/parity");
    ogn::Packet p;expect(ogn::decode(reference,p),"decode public vector");
    expect(std::get<std::string>(p.fields.at("address"))=="123456","address");
    expect(std::abs(number(p,"latitude_deg")-50.123446666666666)<1e-10,"public latitude");
    expect(std::abs(number(p,"longitude_deg")-20.123453333333334)<1e-10,"public longitude");
    expect(number(p,"altitude_msl_m")==1200 && number(p,"altitude_std_m")==1250,"GNSS/baro altitude");
    expect(number(p,"speed_mps")==35.5 && std::abs(number(p,"track_deg")-130.4)<1e-10,"speed and rounded heading");
    expect(number(p,"dop")==1.5 && number(p,"second")==15 && number(p,"fix_dimensions")==3,"DOP includes 1.0 offset");
    expect(number(p,"climb_mps")==4.5 && number(p,"turn_dps")==1.5,"climb/turn");
    expect(ogn::unsignedVr(0x1000,12)==4097 && ogn::unsignedVr(0x2000,12)==12290 && ogn::unsignedVr(0x3fff,12)==61436,"variable altitude ranges");
    expect(ogn::unsignedVr(0x3ff,8)==3836 && ogn::signedVr(0xff,5)==-476 && ogn::signedVr(0x1ff,6)==-956,"VR extremes");
    for(int bit=0;bit<208;++bit) {
        auto bad=reference;bad[bit/8]^=uint8_t(1u<<(bit%8));auto c=ogn::correct(bad,4);
        expect(c.valid && c.corrected==1 && bad==reference,"correct all 208 single-bit locations");
    }
    std::mt19937 random(4312);
    for(int i=0;i<128;++i) {
        auto bad=reference;int a=int(random()%208),b=int(random()%208);while(b==a)b=int(random()%208);
        bad[a/8]^=uint8_t(1u<<(a%8));bad[b/8]^=uint8_t(1u<<(b%8));auto c=ogn::correct(bad,4);
        expect(c.valid && c.corrected==2 && bad==reference,"two-bit LDPC correction");
    }
    auto ambiguous=reference; ambiguous[29/8]^=uint8_t(1u<<(29%8)); ambiguous[70/8]^=uint8_t(1u<<(70%8));
    const auto ambiguousInput=ambiguous;
    expect(!ogn::correct(ambiguous,2).valid && ambiguous==ambiguousInput,"reject non-unique two-error syndrome");
    // Above the conservative radius, multiple valid codewords can be equally
    // close. Exercise BP without claiming recovery is guaranteed for all patterns.
    int correctedThree=0;
    for(int i=0;i<32;++i) {
        auto bad=reference;std::set<int> changed;while(changed.size()<3)changed.insert(int(random()%208));
        for(int b:changed)bad[b/8]^=uint8_t(1u<<(b%8));const auto c=ogn::correct(bad,4);
        if(c.valid){if(bad==reference && c.corrected==3)++correctedThree;else std::cout<<"Three-error case outside guaranteed radius: correction "<<c.corrected<<" bits\n";expect(ogn::parityFailures(bad)==0 && c.corrected<=4,"BP output must meet checks and correction budget");}
    }
    expect(correctedThree>=24,"min-sum corrects deterministic three-error cases");
    auto bad=reference;bad[0]^=1;const auto saved=bad;
    expect(!ogn::correct(bad,0).valid && bad==saved,"correction disabled preserves input");
    std::fill(bad.begin(),bad.end(),0x55);bad[1]^=0x39;expect(!ogn::decode(bad,p,0),"reject failed parity");
    auto invalidHeader=reference;invalidHeader[3]^=8;ogn::encodeParity(invalidHeader);
    expect(!ogn::decode(invalidHeader,p),"reject header parity even with valid LDPC");
    auto custom=reference;custom[3]|=0x40;ogn::encodeParity(custom);
    expect(ogn::decode(custom,p)&&p.kind=="encrypted/custom"&&!p.fields.contains("latitude_deg"),"custom data not dewhitened/interpreted");
    auto negative=pack(0x03123456,{uint32_t((int32_t(-33.9*600000)/8)&0xffffff)|(12u<<24)|(1u<<30),uint32_t((int32_t(-70.7*600000)/16)&0xffffff),0x80000000u|300u,0x40000u});
    expect(ogn::decode(negative,p) && number(p,"latitude_deg")<0 && number(p,"longitude_deg")<0,"signed coordinates");
    expect(!p.fields.contains("climb_mps")&&!p.fields.contains("turn_dps")&&!p.fields.contains("altitude_std_m"),"absent rates/baro stay absent");
    auto noFix=pack(0x03123456,{63u<<24,0,0,0});
    expect(ogn::decode(noFix,p)&&!p.fields.contains("latitude_deg")&&!std::get<bool>(p.fields.at("position_valid")),"invalid fix never displayed as a position");
    const uint32_t status0=72u|(98u<<8)|(12u<<15)|(3u<<20)|(14u<<24)|(1u<<30);
    auto status=pack(0x07123456,{status0,4u|(180u<<8)|(5u<<16)|(0x80u<<24),1000u|(12665u<<14)|(9u<<28),7u|(4u<<8)|(6u<<16)|(200u<<24)});
    expect(ogn::decode(status,p)&&p.kind=="status","status dispatch");
    expect(number(p,"satellites")==9&&number(p,"pressure_hpa")==1013.2&&number(p,"temperature_c")==20.5,"status sensor fields");
    expect(!p.fields.contains("humidity_pct")&&number(p,"radio_noise_dbm")==-90&&number(p,"voltage_v")==8.0625,"status availability and VR battery");
    ogn::Codeword info{};ogn::write32(info.data(),0x07123456);std::string text="TEST";
    for(size_t i=0;i<text.size();++i) for(int b=0;b<7;++b) if((text[i]>>b)&1)info[4+(7*i+b)/8]|=uint8_t(1u<<((7*i+b)%8));
    // registration type 5 terminates the string.
    for(int b=0;b<7;++b)if((5>>b)&1)info[4+(28+b)/8]|=uint8_t(1u<<((28+b)%8));
    info[18]=0x14;for(int i=4;i<19;++i)info[19]^=info[i];
    ogn::whiten(info,false);ogn::encodeParity(info);
    expect(ogn::decode(info,p)&&p.kind=="info"&&std::get<std::string>(p.fields.at("registration"))=="TEST","packed 7-bit info/checksum");
    auto wind=pack(0x07123456,{expected[1],expected[2],expected[3],(expected[4]&0xff0fffff)|0x00200000});
    expect(ogn::decode(wind,p)&&p.kind=="wind"&&p.fields.contains("latitude_deg"),"wind report dispatch");
    const auto js=ogn::jsonFields({{"quote",std::string("a\"b\n")},{"number",2.5}});
    expect(js.find("a\\\"b\\u000a")!=std::string::npos,"JSON escaped strings");
}
std::vector<dect2::modebuilder::cf> signal(double rate,bool invert=false,double cfo=0) {
    namespace mb=dect2::modebuilder;
    std::vector<mb::cf> all;
    for(int packet=0;packet<4;++packet) {
        auto d=ogn::radioDefinition();d.offsetHz=(packet%2) ? 100000 : -100000;
        if(invert)d.polarity=mb::Polarity::Inverted;
        mb::GenOptions g;g.fs=rate;g.snrDb=40;g.cfoHz=cfo;g.seed=uint32_t(19+packet);
        const std::vector<uint8_t> bytes(reference.begin(),reference.end());
        auto iq=mb::generate(d,{bytes},g);all.insert(all.end(),iq.begin(),iq.end());
    }
    return all;
}
void iqTests(const std::string& fixture, bool missing = false) {
    for(double rate:{1250000.,2000000.})for(bool inverted:{false,true}) {
        auto iq=signal(rate,inverted,inverted?-5000:5000);std::vector<float> wire;
        for(auto x:iq){wire.push_back(x.real());wire.push_back(x.imag());}
        for(size_t step:{size_t(137),size_t(16384)}) {
            int received=0,m1=0,m2=0;
            ogn::Decoder decoder({},[&](const ogn::Packet& p){++received;auto c=std::get<std::string>(p.fields.at("channel"));m1+=c=="M1";m2+=c=="M2";expect(number(p,"speed_mps")==35.5,"end-to-end payload value");});
            decoder.configure(868300000,rate);
            for(size_t i=0;i<iq.size();i+=step)decoder.feed(wire.data()+2*i,std::min(step,iq.size()-i),i);
            decoder.flush();decoder.flush();
            expect(received==4 && m1==2 && m2==2,"M1/M2 streaming, polarity and frequency offsets");
        }
    }
    if(!fixture.empty()) {auto iq=signal(1250000);
        if(missing) {
            auto word=reference;ogn::whiten(word,true);
            ogn::write32(&word[8],ogn::read32(&word[8])&~(1u<<30));
            ogn::write32(&word[12],(ogn::read32(&word[12])&0xffffffu)|0x80000000u);
            ogn::write32(&word[16],(ogn::read32(&word[16])&0x00f803ffu)|0x40000u);
            ogn::whiten(word,false);ogn::encodeParity(word);
            auto d=ogn::radioDefinition();d.offsetHz=-100000;d.sampleRate=1250000;
            dect2::modebuilder::GenOptions g;g.snrDb=40;g.seed=71;
            iq=dect2::modebuilder::generate(d,{std::vector<uint8_t>(word.begin(),word.end())},g);
        }
        std::ofstream out(fixture,std::ios::binary);out.write(reinterpret_cast<const char*>(iq.data()),std::streamsize(iq.size()*sizeof(iq[0])));expect(bool(out),"write synthetic fixture");}
    ogn::Options o;o.mode="m1";o.centreOverrideHz=868300000;
    ogn::Decoder override(o,[](const auto&){});override.configure(868200000,1250000);
    expect(override.streamDescription().find("-100.000000 kHz")!=std::string::npos,"file centre override");
    bool refused=false;try{ogn::Decoder d({},[](const auto&){});d.configure(868200000,400000);}catch(...){refused=true;}
    expect(refused,"refuse out-of-band channels");
}
}
int main(int argc,char** argv) {
    try {protocolTests();iqTests(argc==3?argv[2]:"",argc==3&&std::string(argv[1])=="--missing-fixture");std::cout<<checks<<" checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
