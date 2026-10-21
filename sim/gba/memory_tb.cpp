// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_memory.h"
#include "verilated.h"
#include <iostream>
#include <stdexcept>
double sc_time_stamp(){return 0;}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try{
        Vgba_memory d;
        auto tick=[&](){d.clk=0;d.eval();d.clk=1;d.eval();};
        auto check=[&](bool ok,const char*s){if(!ok)throw std::runtime_error(s);};
        auto reset=[&](){d.reset=1;d.req=0;d.mem_ack=0;d.mem_rvalid=0;tick();d.reset=0;};
        auto start=[&](unsigned a,bool write,unsigned value=0,unsigned mask=15){
            d.addr=a;d.we=write;d.wdata=value;d.be=mask;d.req=1;tick();
        };
        auto finish=[&](){d.req=0;d.mem_ack=0;d.mem_rvalid=0;tick();};
        reset();start(0,false);check(d.done && d.rdata==0xe59ff018,"bootstrap read");finish();
        start(0x04000000,true,0x1234,1);check(d.done,"DISPCNT byte write");finish();
        start(0x04000000,false);check(d.rdata==0x0034,"DISPCNT low lane");finish();
        start(0x04000002,true,0xab00,2);finish();
        start(0x04000000,false);check(d.rdata==0xab34,"DISPCNT high lane");finish();
        start(0x02040004,true,0x11223344,5);
        check(d.mem_req && d.mem_we && d.mem_addr==0x180001 && d.mem_be==5 && d.mem_wdata==0x11223344,"EWRAM request translation");
        for(int i=0;i<7;i++){tick();check(!d.done && d.mem_req,"write completed before acceptance");}
        d.mem_ack=1;tick();check(d.done,"write completion missing");finish();
        start(0x03008004,false);
        check(d.mem_req && !d.mem_we && d.mem_addr==0x190001,"IWRAM mirror translation");
        d.mem_ack=1;tick();d.mem_ack=0;
        for(int i=0;i<9;i++){tick();check(!d.done && !d.mem_req,"read completed without data or reissued");}
        d.mem_rvalid=1;d.mem_rdata=0x89abcdef;tick();
        check(d.done && d.rdata==0x89abcdef,"read response missing");finish();
        for(auto a:{0x00000000u,0x08000000u}){
            reset();start(a,true,1);check(d.done && d.fault && !d.mem_req,"readonly write was accepted");finish();
        }
        for(auto a:{0x08600000u,0x04000204u,0x10000000u}){
            reset();start(a,false);check(d.done && d.fault && !d.mem_req,"unsupported access falsely accepted");finish();
        }
        reset();start(0x02000000,false);check(d.mem_req,"missing pending request");
        reset();check(!d.mem_req && !d.done && !d.fault,"reset did not cancel request");
        start(0,false);check(d.done && !d.fault,"reboot failed");finish();
        std::cout<<"PASS memory bus: byte masks, mirrors, delayed accept/read completion, no duplicate requests, ROM/unsupported faults, reset cancellation\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
