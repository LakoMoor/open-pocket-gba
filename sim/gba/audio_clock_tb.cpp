// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vgba_audio_clock_test.h"
#include "verilated.h"
#include <iostream>
#include <stdexcept>
double sc_time_stamp(){return 0;}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);
    try{
        Vgba_audio_clock_test d;
        auto tick=[&](){d.clk=0;d.eval();d.clk=1;d.eval();};
        d.reset=1;tick();tick();d.reset=0;
        unsigned half=0,rises=0,wraps=0;
        bool old_audio=d.audio_clk,old_wrap=d.wrap;
        for(unsigned i=0;i<108000;i++){
            tick();++half;
            if(d.audio_clk!=old_audio){
                if(half!=281 && half!=282)throw std::runtime_error("audio half-period");
                half=0;if(d.audio_clk)++rises;old_audio=d.audio_clk;
            }
            if(d.n_value!=6144)throw std::runtime_error("HDMI ACR N mismatch");
            if(d.wrap!=old_wrap){
                ++wraps;old_wrap=d.wrap;
                if(wraps>1 && d.cts!=27000)throw std::runtime_error("HDMI ACR CTS mismatch");
            }
        }
        if(rises!=192 || wraps!=4)throw std::runtime_error("48 kHz rate or ACR liveness");
        d.reset=1;tick();if(d.audio_clk)throw std::runtime_error("audio reset");
        std::cout<<"PASS HDMI audio clock: 48 kHz average, 281/282 half-periods, real ACR module N=6144 CTS=27000, reset\n";
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
