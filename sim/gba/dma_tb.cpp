// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_dma.h"
#include "verilated.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
double sc_time_stamp(){return 0;}
struct Tx {uint32_t addr,data,mask;bool write;};
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try{
        Vgba_dma d;
        std::unordered_map<uint32_t,uint32_t> mem;
        std::vector<Tx> transfers;
        uint32_t random=37;unsigned irq[4]={},errors=0,cycles=0;
        bool waiting=false;Tx held{};unsigned delay=0;
        bool inject_error=false;
        auto check=[&](bool ok,const char*s){if(!ok)throw std::runtime_error(s);};
        auto step=[&](){
            d.clk=0;d.bus_done=0;d.bus_error=0;d.eval();
            if(!d.reset){
                if(waiting){
                    check(d.bus_req && d.bus_addr==held.addr && bool(d.bus_we)==held.write && d.bus_be==held.mask && d.bus_wdata==held.data,"request changed before completion");
                    if(--delay==0){
                        d.bus_done=1;d.bus_error=inject_error;inject_error=false;
                        d.bus_rdata=mem[held.addr&~3u];
                        if(!d.bus_error && held.write){
                            auto& value=mem[held.addr&~3u];
                            for(unsigned b=0;b<4;b++) if(held.mask&(1<<b)) value=(value&~(255u<<(b*8)))|(held.data&(255u<<(b*8)));
                        }
                        transfers.push_back(held);waiting=false;
                    }
                }else if(d.bus_req){
                    held={d.bus_addr,d.bus_wdata,d.bus_be,bool(d.bus_we)};
                    waiting=true;random=random*1664525+1013904223;delay=1+random%9;
                }
            }
            d.eval();d.clk=1;d.eval();cycles++;
            for(unsigned c=0;c<4;c++) irq[c]+=bool(d.irq_events&(1<<c));
            errors+=bool(d.unsupported);
        };
        auto reset=[&](){
            d.reset=1;d.ce=1;d.access=0;d.we=0;d.bus_done=0;d.hblank_start=0;d.vblank_start=0;
            waiting=false;inject_error=false;step();d.reset=0;transfers.clear();
            for(auto& n:irq)n=0;errors=0;
        };
        auto write=[&](unsigned a,unsigned v,unsigned mask=15){
            d.addr=a;d.wdata=v;d.be=mask;d.access=1;d.we=1;step();d.access=0;d.we=0;
        };
        auto control=[&](unsigned c){d.addr=0xb8+c*12;d.eval();check(d.selected,"missing DMA CNT decode");return d.rdata>>16;};
        auto setup=[&](unsigned c,unsigned src,unsigned dst,unsigned count,unsigned ctl){
            write(0xb0+c*12,src);write(0xb4+c*12,dst);write(0xb8+c*12,count|(ctl<<16));
        };
        auto drain=[&](unsigned limit=2000000){
            for(unsigned i=0;i<limit;i++){d.eval();if(!d.busy && !waiting)return;step();}
            throw std::runtime_error("DMA timeout");
        };
        auto pulse=[&](bool vblank){d.hblank_start=!vblank;d.vblank_start=vblank;step();d.hblank_start=0;d.vblank_start=0;};

        // Every source/dest mode and transfer width, including opposing halfword lanes.
        for(unsigned width:{2u,4u})for(unsigned sm=0;sm<3;sm++)for(unsigned dm=0;dm<4;dm++){
            reset();mem.clear();
            for(unsigned a=0x2000000;a<0x2000100;a+=4)mem[a]=0xa5000000u^(a*12345u);
            unsigned src=0x2000042,dst=0x3000080;
            unsigned s=src&~(width-1),t=dst&~(width-1);
            std::unordered_map<uint32_t,uint32_t> expected;
            for(unsigned n=0;n<7;n++){
                unsigned value=mem[s&~3u];
                if(width==2)value=(value>>((s&2)*8))&65535;
                auto& old=expected[t&~3u];
                if(width==4)old=value;else old=(old&~(65535u<<((t&2)*8)))|(value<<((t&2)*8));
                s+=sm==0?width:sm==1?-width:0;
                t+=dm==0||dm==3?width:dm==1?-width:0;
            }
            setup(3,src,dst,7,0xc000|(width==4?0x400:0)|(sm<<7)|(dm<<5));
            drain();check(irq[3]==1 && !(control(3)&0x8000),"one-shot IRQ/disable");
            check(transfers.size()==14,"duplicate/lost DMA unit");
            for(auto [a,v]:expected)check(mem[a]==v,"DMA mode/lane payload mismatch");
        }
        // Visible HBlank repeat: destination reloads; source continues, unlike SAD.
        reset();mem.clear();mem[0x2000000]=0x11112222;mem[0x2000004]=0x33334444;
        setup(1,0x2000000,0x3000002,2,0xe260); // HBlank, repeat, IRQ, dest reload, halfword
        for(unsigned i=0;i<8;i++)step();check(!d.busy && transfers.empty(),"timed DMA ran before trigger");
        pulse(false);drain();check(mem[0x3000000]==0x22220000 && mem[0x3000004]==0x1111,"first repeat");
        pulse(false);drain();check(mem[0x3000000]==0x44440000 && mem[0x3000004]==0x3333,"source continuation/dest reload");
        check(irq[1]==2 && (control(1)&0x8000),"repeat IRQ/enable");
        mem[0x2000008]=0x55556666;mem[0x200000c]=0x77778888;
        write(0xbc,0x02000800); // SAD updates must not reset the current source.
        write(0xc0,0x03000102);write(0xc4,3,3);
        pulse(false);drain();
        check(mem[0x3000100]==0x66660000 && mem[0x3000104]==0x88885555 && irq[1]==3,"repeat failed to reload updated CNT_L/DAD or reloaded SAD");
        write(0xc6,0,8);pulse(false);drain();check(transfers.size()==14,"disabled repeat retriggered");
        reset();mem[0x2000000]=0x11112222;
        setup(1,0x2000000,0x3000200,2,0xe260);
        write(0xc0,0x03000300);write(0xc4,1,3);
        pulse(false);drain();check(transfers.size()==4 && transfers[1].addr==0x3000200,"first timed block ignored enable-time latches");
        pulse(false);drain();check(transfers.size()==6 && transfers[5].addr==0x3000300,"repeat did not use new registers at trigger");
        // All channels pending together: fixed priority 0 > 1 > 2 > 3.
        reset();mem.clear();for(unsigned c=0;c<4;c++)setup(c,0x2000000+c*4,0x3000000+c*4,1,0xd400);
        for(unsigned i=0;i<3;i++)step();pulse(true);drain();
        check(transfers.size()==8,"priority transfer count");
        for(unsigned c=0;c<4;c++)check(transfers[c*2].addr==0x2000000+c*4 && irq[c]==1,"DMA priority order");
        // A higher-priority request interrupts DMA3 after its current read/write pair.
        reset();setup(0,0x2000080,0x3000080,1,0xd400);setup(3,0x2000000,0x3000000,5,0xc400);
        while(transfers.empty())step();pulse(true);drain();
        check(transfers[0].addr==0x2000000 && transfers[1].write && transfers[2].addr==0x2000080 && transfers[3].write,"priority preempted inside read/write pair");
        // Enable startup is exactly two GBA ticks, not two system clocks.
        reset();d.ce=0;setup(0,0x2000000,0x3000000,1,0x8000);
        for(unsigned i=0;i<10;i++){step();check(d.busy && !d.bus_req,"startup ignored ce");}
        d.ce=1;step();d.ce=0;step();check(!d.bus_req,"startup too early");
        d.ce=1;step();d.ce=0;step();check(d.bus_req,"startup missing second tick");drain();
        // Zero count is maximum, including DMA3's 65536 case. Address masks apply.
        for(unsigned c:{0u,3u}){
            reset();setup(c,0xf2000003,0xf3000003,0,0xc540); // fixed source/dest, word
            drain();unsigned n=c==3?65536:16384;
            check(transfers.size()==n*2 && irq[c]==1,"zero count maximum mismatch");
            check(transfers[0].addr==0x2000000 && transfers[1].addr==0x3000000,"address mask/alignment");
        }
        // Byte writes merge SAD and count, reserved bits stay zero on CNT readback.
        reset();write(0xd4,0x02000000);write(0xd5,0x00002400,2);write(0xd8,0x03000000);
        write(0xdc,3,1);write(0xde,0x84000000,8);drain();
        check(transfers.size()==6 && transfers[0].addr==0x02002400,"partial register writes");
        reset();setup(3,0x2000000,0x3000000,2,0xf400);step();
        check(errors==1 && !d.busy && transfers.empty() && !(control(3)&0x8000),"unsupported capture falsely ran");
        reset();setup(1,0x2000000,0x3000000,2,0x8180);step();check(errors==1,"prohibited source mode accepted");
        reset();setup(3,0x2000000,0x3000000,2,0x8800);step();check(errors==1,"DRQ falsely accepted");
        reset();setup(3,0x2000000,0x3000000,2,0x9400);write(0xde,0xb4000000,12);step();
        check(errors==1 && !d.busy && !(control(3)&0x8000),"unsupported mode update on enabled channel was ignored");
        reset();setup(2,0x2000000,0x3000000,3,0xc400);inject_error=true;drain();
        check(transfers.size()==1 && !irq[2] && !(control(2)&0x8000),"bus fault did not abort DMA");
        reset();setup(3,0x2000000,0x3000000,3,0x8400);while(!d.bus_req)step();reset();
        for(unsigned i=0;i<20;i++)step();check(!d.busy && !d.bus_req && transfers.empty(),"reset did not cancel DMA");
        std::cout<<"PASS DMA: 24 width/address-mode combinations, random bus stalls, halfword lanes, repeat/reload, 4-channel priority/preemption, two-tick startup, 16384/65536 counts, byte writes, masks, IRQ, unsupported modes, abort/reset; "<<cycles<<" clocks\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
