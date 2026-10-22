// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_memory.h"
#include "verilated.h"
#include <iostream>
#include <stdexcept>
#include <vector>
double sc_time_stamp(){return 0;}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try{
        Vgba_memory d;
        std::vector<uint32_t> mem(0x200000);
        bool busy=false,reading=false;unsigned delay=0,a=0,data=0,mask=0,value=0,rng=41;
        unsigned writes=0,clocks=0;bool is_write=false;
        bool delay_cpu_read=false,saw_dma_waiting=false;
        auto check=[&](bool ok,const char*s){if(!ok)throw std::runtime_error(s);};
        auto step=[&](){
            d.clk=0;d.mem_ack=0;d.mem_rvalid=0;d.eval();
            if(!d.reset){
                if(busy && delay)--delay;
                if(busy && !delay){
                    if(reading){d.mem_rvalid=1;d.mem_rdata=data;busy=false;}
                    else{
                        check(d.mem_req && d.mem_addr==a && bool(d.mem_we)==is_write && d.mem_be==mask && d.mem_wdata==value,"SDRAM request changed before accept");
                        d.mem_ack=1;
                        if(is_write){
                            for(unsigned b=0;b<4;b++)if(mask&(1<<b))mem[a]=(mem[a]&~(255u<<(b*8)))|(value&(255u<<(b*8)));
                            ++writes;busy=false;
                        }else{
                            reading=true;data=mem[a];
                            delay=delay_cpu_read && a==0 ? 500000 : 3+rng%7;
                            if(a==0)delay_cpu_read=false;
                        }
                    }
                }else if(!busy && d.mem_req){
                    busy=true;reading=false;a=d.mem_addr;mask=d.mem_be;value=d.mem_wdata;is_write=d.mem_we;
                    rng=rng*1664525u+1013904223u;delay=1+rng%11;
                }
            }
            d.eval();d.clk=1;d.eval();clocks++;
            saw_dma_waiting|=bool(d.dma_active && busy && reading && a==0);
        };
        auto reset=[&](){
            d.req=0;d.keys=0;d.reset=1;busy=false;delay_cpu_read=false;saw_dma_waiting=false;
            step();d.reset=0;writes=0;
        };
        auto access=[&](unsigned addr,bool we,unsigned value=0,unsigned mask=15){
            d.addr=addr;d.we=we;d.wdata=value;d.be=mask;d.req=1;
            for(unsigned i=0;i<2000000;i++){
                step();if(d.done){unsigned result=d.rdata;d.req=0;step();return result;}
            }
            throw std::runtime_error("CPU response timeout/deadlock");
        };
        auto setup=[&](unsigned c,unsigned src,unsigned dst,unsigned n,unsigned ctl){
            unsigned base=0x040000b0+c*12;
            access(base,true,src);access(base+4,true,dst);access(base+8,true,n|(ctl<<16));
        };
        auto idle=[&](unsigned n){for(unsigned i=0;i<n;i++)step();};

        reset();mem[0]=0xaabbccdd;
        for(unsigned n=0;n<32;n++)mem[0x600000/4+n]=n*0x1020304;
        setup(3,0x02000000,0x03000100,32,0xc400);
        check(access(0x08000000,false)==0xaabbccdd,"DMA response leaked into CPU fetch");
        check(writes==32 && !d.dma_active && !d.fault,"CPU was served before DMA block completed");
        for(unsigned n=0;n<32;n++)check(mem[0x640100/4+n]==n*0x1020304,"DMA memory map payload");
        check((access(0x04000200,false)>>16)==0x800 && !d.irq,"DMA IF missing with IE disabled");
        access(0x04000200,true,0x800,3);access(0x04000208,true,1,1);check(d.irq,"DMA IE/IME delivery");
        access(0x04000200,true,0x08000000,12);check(!d.irq,"DMA IF W1C failed");
        check(!(access(0x040000dc,false)&0x80000000),"DMA enable did not clear");

        // VBlank arrives while a CPU read is already in flight. Its response
        // must still reach the CPU before the waiting DMA can own the bus.
        reset();setup(3,0x02000000,0x03000400,2,0xd400);delay_cpu_read=true;
        check(access(0x08000000,false)==0xaabbccdd && saw_dma_waiting && writes==0,
              "VBlank DMA stole/lost an in-flight CPU response");
        check(access(0x08000000,false)==0xaabbccdd && writes==2 && !d.dma_active && !d.fault,
              "waiting DMA did not run before the next CPU request");
        check(mem[0x640404/4]==0x01020304,"queued DMA payload after CPU response");

        // Exactly 160 visible HBlank transfers; no DMA during VBlank/line 227.
        reset();for(unsigned p=0;p<160;p++){
            auto& v=mem[0x600100/4+p/2];unsigned shift=(p%2)*16;
            v=(v&~(65535u<<shift))|((p+1)<<shift);
        }
        mem[0x640200/4]=0xdeadbeef;
        setup(1,0x02000100,0x03000200,1,0xe260);
        bool reached=false;
        for(unsigned i=0;i<450;i++){
            idle(1024);unsigned line=access(0x04000004,false)>>16;
            if(line>=160){reached=true;break;}
        }
        check(reached && writes==160 && mem[0x640200/4]==0xdead00a0,"visible HBlank count/halfword reload");
        idle(150000); // Remains within lines 160..227, before next frame's HBlank.
        check(writes==160 && !d.fault,"HBlank DMA incorrectly ran during VBlank");
        access(0x040000c4,true,0,8); // Disable while waiting for the next HBlank.
        check((access(0x04000200,false)>>16)==0x200,"repeat DMA IRQ channel routing");

        reset();setup(2,0x02000000,0x03000300,2,0xd400);idle(4000);
        check(writes==0 && !d.dma_active,"VBlank DMA started early");
        for(unsigned i=0;i<450 && writes<2;i++)idle(1024);
        check(writes==2 && !d.fault,"VBlank DMA did not complete");
        check((access(0x04000200,false)>>16)==0x400,"DMA2 IRQ routing");

        reset();mem[0x600000/4]=0x0403;setup(3,0x02000000,0x04000000,1,0x8000);
        check(access(0x04000000,false)==0x0403 && !d.fault,"DMA IO/halfword write");
        reset();setup(3,0x02000000,0x08000000,2,0xc400);access(0x08000000,false);
        check(d.fault && d.fault_addr==0x08000000 && !d.dma_active && writes==0,"DMA readonly fault/abort");
        reset();setup(3,0x02000000,0x040000d4,2,0x8400);access(0x08000000,false);
        check(d.fault && d.fault_addr==0x040000d4 && !d.dma_active,"DMA self-register access must fault");
        reset();setup(3,0x02000000,0x03000000,2,0xb400);idle(2);
        check(d.fault && d.fault_addr==0x040000dc && !d.dma_active && writes==0,"unsupported DMA mode not observable");
        std::cout<<"PASS DMA memory integration: CPU response ownership/blocking including VBlank during 500000-clock CPU read, 32-word copy under random SDRAM latency, IE/IF/IME, 160 visible HBlanks and VBlank exclusion, VBlank start, IO halfword write, readonly/self-register/unsupported faults; "<<clocks<<" clocks\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
