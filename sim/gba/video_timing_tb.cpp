// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_video_timing.h"
#include "verilated.h"
#include <iostream>
#include <stdexcept>
double sc_time_stamp(){return 0;}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try {
        Vgba_video_timing d;
        auto step=[&](){d.clk=0;d.eval();d.clk=1;d.eval();};
        auto check=[&](bool ok,const char*s){if(!ok)throw std::runtime_error(s);};
        auto reset=[&](){d.ce=0;d.access=0;d.we=0;d.addr=4;d.reset=1;step();d.reset=0;d.eval();};
        auto write=[&](unsigned v,unsigned mask){
            d.ce=0;d.addr=4;d.wdata=v;d.be=mask;d.access=1;d.we=1;step();
            d.access=0;d.we=0;d.eval();
        };
        reset();check(d.selected && d.rdata==4,"reset status/compare");
        // Mixed read-only/status/reserved bits must survive a full word write.
        write(0xffffffff,15);
        check(d.rdata==0xff38 && d.vcount==0,"DISPSTAT masks or read-only VCOUNT");
        write(0x00a00000,12);check(d.rdata==0xff38,"upper halfword changed VCOUNT or DISPSTAT");
        write(0,2);check(d.rdata==0x003c,"compare byte write");
        check(d.irq_events==4,"compare write did not raise match IRQ");
        step();check(d.irq_events==0,"match IRQ repeated while ce stopped");
        write(0,1);write(0x38,1);check(d.irq_events==0,"IRQ enable retriggered existing match");
        write(0x0000ff00,2);check(d.irq_events==0,"out-of-range compare matched");
        // Three complete frames: inspect every GBA clock and insert sysclock gaps.
        // Expected status is calculated directly from absolute time, not DUT counters.
        reset();write((73<<8)|0x38,3);
        unsigned h_edges=0,v_edges=0,c_edges=0,sysclocks=0;
        for(unsigned n=1;n<=3*280896;n++){
            unsigned gaps=(n*17u)%4;
            d.ce=0;
            for(unsigned g=0;g<gaps+1;g++){
                unsigned old=d.rdata;step();sysclocks++;
                check(d.rdata==old,"video counters moved without GBA tick");
                check(d.irq_events==0 && !d.hblank_start && !d.vblank_start,"event persisted during stalled tick");
            }
            d.ce=1;step();sysclocks++;
            unsigned line=(n/1232)%228,cycle=n%1232;
            unsigned flags=(line>=160 && line<227 ? 1:0)|(cycle>=1006?2:0)|(line==73?4:0);
            unsigned events=(cycle==0 && line==160?1:0)|(cycle==1006?2:0)|(cycle==0 && line==73?4:0);
            check(d.vcount==line && d.line_cycle==cycle,"line/frame period mismatch");
            check(d.rdata==((line<<16)|(73<<8)|0x38|flags),"DISPSTAT/VCOUNT boundary mismatch");
            check(d.irq_events==events,"video IRQ edge mismatch");
            check(d.hblank_start==bool(events&2) && d.vblank_start==bool(events&1),"blanking strobe mismatch");
            h_edges+=bool(events&2);v_edges+=bool(events&1);c_edges+=bool(events&4);
        }
        check(h_edges==684 && v_edges==3 && c_edges==3,"frame event counts");
        // Counter wraps are not gated by DISPCNT/forced blank or IRQ enables.
        write(0,1);d.ce=1;
        for(unsigned n=0;n<1232;n++){step();check(d.irq_events==0,"disabled video IRQ");}
        check(d.vcount==1,"disabled IRQ stopped video clock");
        reset();check(d.rdata==4 && !d.irq_events && !d.hblank_start && !d.vblank_start,"warm reset");
        d.addr=0;d.eval();check(!d.selected,"incorrect video register decode");
        std::cout<<"PASS video timing: 3 frames/842688 GBA ticks, "<<sysclocks
                 <<" system clocks; 684 HBlank/3 VBlank/3 VCount edges, line 227, byte masks, compare writes, enable gating, reset\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
