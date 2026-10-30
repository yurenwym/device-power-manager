#include "../src/protocols.hpp"
#include <iostream>
#include <cstdlib>
using namespace battery::protocol;
static int checks=0;
void check(bool v) { ++checks; if(!v){std::cerr<<"Failed check "<<checks<<'\n';std::exit(1);} }
Bytes packet(uint8_t command, uint8_t length, uint8_t level, uint8_t status) {
    auto b=compx_query(command); b[4]=length; b[5]=level; b[6]=status;
    uint8_t sum=0; for(size_t i=0;i<19;++i)sum+=b[i]; b[19]=sum; return b;
}
int main() {
    Bytes b={5,0x15,0,0,1,0,0,0,0,86,0,0,1};
    check(sora(b)->percent==86); b[12]=0;check(!sora(b));b[12]=1;b[9]=101;check(!sora(b));
    b[9]=0;check(sora(b)->percent==0);b[0]=6;check(!sora(b));check(!sora({}));
    check(hyperx({0x66,0x89,0x0f,0xaf,87})->percent==87);
    check(hyperx({0x66,0x0d,1,0,0})->percent==0);
    check(!hyperx({0x66,0x89,0,0,87}));check(!hyperx({0x66,0x89,1,0,101}));
    check(!hyperx({0x66,0x8a,1,0,10}));check(!hyperx({}));
    check(hyperx_charge({0x66,0x8a,2})==2);check(!hyperx_charge({0x66,0x8a,3}));
    b=packet(0x4a,2,90,0);check(compx(b)->percent==90);check(!compx(b)->charging.value());
    b=packet(0x4a,2,100,1);check(compx(b)->full);check(!compx(b)->charging.value());
    b=packet(0x4a,2,40,0x10);check(compx(b)->charging.value());
    b[19]^=1;check(!compx(b));b=packet(0x4a,2,101,0);check(!compx(b));
    b=packet(0x4a,2,10,2);check(!compx(b));b=packet(0x4a,3,10,0);check(!compx(b));
    b=packet(0x4a,2,0,0);check(compx(b)->percent==0);b.pop_back();check(!compx(b));check(!compx({}));
    b=packet(5,6,10,0);b[10]=0x10;b[19]+=0x10;
    check(compx_identity(b)->at(5)==0x10);b[19]++;check(!compx_identity(b));check(!compx_identity({}));
    check(compx_query(5)[19]==0x19);check(compx_query(0x4a)[19]==0x5e);
    std::cout<<checks<<" protocol checks passed\n";
}
