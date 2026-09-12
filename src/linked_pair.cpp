#include "dmg/linked_pair.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace dmg {
struct LinkedPair::Impl {
    struct Pulse { std::uint64_t cycle=UINT64_MAX; bool bit=true, reply=true; };
    struct Port { Impl *pair; unsigned side; };
    std::array<Bus *,2> buses;
    std::array<Cpu *,2> cpus;
    std::array<Port,2> ports;
    std::array<SerialEndpoint,2> endpoints{};
    std::array<Pulse,2> pulses{};
    std::array<bool,2> running{};
    std::uint64_t target{}, edge_count{};
    Impl(Bus &a,Cpu &ac,Bus &b,Cpu &bc):buses{&a,&b},cpus{&ac,&bc},ports{{{this,0},{this,1}}} {
        if(a.serial_endpoint || b.serial_endpoint || a.cycles()!=b.cycles())
            throw std::runtime_error("Linked Game Boys must start together with no other cable attached.");
        target=a.cycles();
        for(unsigned i=0;i<2;++i){
            endpoints[i]={&ports[i],nullptr,tick,edge};
            buses[i]->serial_endpoint=&endpoints[i];
        }
    }
    ~Impl(){ for(unsigned i=0;i<2;++i)if(buses[i]->serial_endpoint==&endpoints[i])buses[i]->serial_endpoint=nullptr; }
    void step(unsigned side){
        if(running[side])throw std::runtime_error("Both Game Boys selected internal link clocks. This cable timing is not supported yet.");
        running[side]=true;
        const auto before=buses[side]->cycles();
        try { cpus[side]->step(); } catch(...){running[side]=false;throw;}
        running[side]=false;
        if(buses[side]->cycles()==before)throw std::runtime_error("This game's STOP state cannot advance a linked session.");
    }
    static void tick(void *context,Bus &bus,std::uint64_t cycle){
        const auto &port=*static_cast<Port *>(context);auto &pulse=port.pair->pulses[port.side];
        if(pulse.cycle!=cycle)return;
        pulse.reply=(bus.serial_data()&0x80U)!=0;
        if(bus.serial_active()&&!bus.serial_internal())static_cast<void>(bus.serial_clock(pulse.bit));
    }
    static bool edge(void *context,Bus &bus,std::uint64_t cycle,bool outgoing){
        const auto &port=*static_cast<Port *>(context);auto &self=*port.pair;const auto other=1U-port.side;
        ++self.edge_count;
        // Coincident internal clocks sample the two old MSBs before either
        // machine shifts. Every external bit is delivered on the slave's dot.
        if(self.pulses[port.side].cycle==cycle)return self.pulses[port.side].bit;
        if(self.buses[other]->cycles()>=cycle)
            throw std::runtime_error("Game Boy cable event passed its receiving clock.");
        auto &pulse=self.pulses[other];pulse={cycle,outgoing,true};
        while(self.buses[other]->cycles()<cycle)self.step(other);
        static_cast<void>(bus);
        return pulse.reply;
    }
    void frame(){
        target+=70224;
        while(buses[0]->cycles()<target || buses[1]->cycles()<target){
            unsigned side=buses[0]->cycles()<=buses[1]->cycles()?0:1;
            const auto other=1U-side;
            // An instruction can span an edge. Run its clock source first so
            // the receiving instruction sees the pulse at its exact bus tick.
            if(buses[other]->next_serial_clock()<=buses[side]->cycles()+24 &&
               buses[other]->next_serial_clock()<buses[side]->next_serial_clock())side=other;
            step(side);
        }
    }
};
LinkedPair::LinkedPair(Bus &a,Cpu &ac,Bus &b,Cpu &bc):impl_(std::make_unique<Impl>(a,ac,b,bc)){}
LinkedPair::~LinkedPair()=default;
void LinkedPair::run_frame(){impl_->frame();}
std::uint64_t LinkedPair::edges()const{return impl_->edge_count;}
}
