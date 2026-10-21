// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_uart_test.h"
#include "verilated.h"
#include <iostream>
#include <stdexcept>
#include <string>
double sc_time_stamp(){return 0;}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try{
        Vgba_uart_test d;
        auto tick=[&](unsigned n=1){for(unsigned i=0;i<n;i++){d.clk=0;d.eval();d.clk=1;d.eval();}};
        auto set=[&](unsigned low){d.snapshot[0]=low;d.snapshot[1]=0x89abcdef;d.snapshot[2]=0x08000218;d.snapshot[3]=0x474201e0;};
        auto byte=[&](){
            bool seen=false;
            for(int i=0;i<4000;i++){bool old=d.tx;tick();if(old && !d.tx){seen=true;break;}}
            if(!seen)throw std::runtime_error("UART start timeout");
            tick(2);if(d.tx)throw std::runtime_error("UART start bit");
            unsigned v=0;for(int b=0;b<8;b++){tick(4);v|=unsigned(d.tx)<<b;}
            tick(4);if(!d.tx)throw std::runtime_error("UART stop bit");return char(v);
        };
        d.reset=1;set(0x12345678);tick(2);d.reset=0;
        std::string a,b;
        for(int i=0;i<33;i++){a+=byte();if(i==0)set(0x87654321);}
        for(int i=0;i<33;i++)b+=byte();
        if(a!="474201e00800021889abcdef12345678\n" || b!="474201e00800021889abcdef87654321\n")
            throw std::runtime_error("UART framing or snapshot atomicity");
        d.reset=1;tick();if(!d.tx)throw std::runtime_error("UART reset idle");
        std::cout<<"PASS UART: 8N1 timing, hex encoding, atomic snapshot, periodic repeat, reset idle\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
