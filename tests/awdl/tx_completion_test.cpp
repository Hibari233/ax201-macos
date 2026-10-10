// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlTxCompletion.h>
#include <AWDL/AwdlAck.h>
#include <AWDL/AwdlRadioObservation.h>
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
using namespace ItlAwdl;

int main() {
    const uint8_t native[6] = {2,1,2,3,4,5};
    uint8_t cts[10] = {0xc4,0,0x40,0x1f,2,6,7,8,9,10}; // 8000 us
    ObservedReservation nav;
    assert(!nav.remaining(100,1000));
    for (size_t n=0;n<10;++n) assert(!nav.observe(cts,n,100,1000,native));
    assert(nav.observe(cts,10,100,1000,native));
    assert(nav.remaining(100,1000)==8000 && nav.remaining(8000,2000)==100);
    assert(!nav.remaining(8100,2000) && !nav.remaining(8101,2000));
    assert(!nav.remaining(100,999) && !nav.remaining(100,101001));
    cts[2]=1; cts[3]=0;
    assert(nav.observe(cts,10,200,1100,native)); // Never shorten a longer observed reservation.
    assert(nav.remaining(300,1200)==7800);
    cts[2]=0; assert(!nav.observe(cts,10,300,1200,native));
    cts[2]=0xff; cts[3]=0xc0; assert(!nav.observe(cts,10,300,1200,native));
    cts[3]=0x7f; assert(nav.observe(cts,10,300,1200,native));
    assert(nav.remaining(300,1200)==32767);
    assert(!nav.observe(nullptr,10,300,1200,native));
    assert(!nav.observe(cts,10,300,1200,nullptr));
    memcpy(cts+4,native,6); assert(!nav.observe(cts,10,300,1200,native));
    cts[4]^=2;
    assert(!nav.observe(cts,4097,300,1200,native));
    cts[0]=0xd0; assert(!nav.observe(cts,10,300,1200,native));
    cts[0]=0xcc; assert(!nav.observe(cts,10,300,1200,native));
    cts[0]=0xc5; assert(!nav.observe(cts,10,300,1200,native));
    cts[0]=0xc4; cts[2]=0x20; cts[3]=0;
    nav={};
    assert(nav.observe(cts,10,0xfffffff0U,2000,native));
    assert(nav.remaining(0xfffffff0U,2000)==32 && nav.remaining(8,2001)==8);
    assert(!nav.remaining(16,2002));
    assert(nav.observe(cts,10,100,200001,native)); // New lease/old host observation is stale.
    assert(nav.remaining(100,200001)==32);
    uint8_t observed[24] = {0xd0,0,0x80,0x01}; // 384 us Duration, management
    PendingRxObservation rx;
    for (size_t n=0;n<24;++n) rx.observe(observed,n,100,300);
    assert(!rx.frames);
    rx.observe(observed,24,100,300);
    assert(rx.frames==1 && rx.management==1 && rx.bytes==24 && rx.maxNavUs==384);
    assert(rx.firstOffsetUs==200 && rx.lastOffsetUs==200);
    observed[0]=0xa4; observed[3]=0xc0; // PS-Poll AID must not inflate NAV.
    rx.observe(observed,16,100,400);
    assert(rx.frames==2 && rx.control==1 && rx.maxNavUs==384);
    observed[0]=8;
    rx.observe(observed,24,0xfffffff0U,0x20);
    assert(rx.data==1 && rx.frames==3 && rx.firstOffsetUs==48 && rx.lastOffsetUs==300);
    const auto savedRx=rx;
    rx.observe(nullptr,24,100,300); rx.observe(observed,4097,100,300);
    rx.observe(observed,24,100,99); rx.observe(observed,24,100,100);
    rx.observe(observed,24,100,100101);
    observed[0]=12; rx.observe(observed,24,100,300);
    observed[0]=1; rx.observe(observed,24,100,300);
    assert(!memcmp(&rx,&savedRx,sizeof(rx)));
    observed[0]=8;
    for (unsigned i=0;i<5000;++i) rx.observe(observed,24,100,500);
    assert(rx.frames==4096 && rx.management+rx.control+rx.data==rx.frames);
    const uint8_t sender[6] = {2,1,2,3,4,5};
    uint8_t ack[14] = {0xd4,0,0,0,2,1,2,3,4,5,0,0,0,0};
    uint32_t delay = 0xfeed;
    for (size_t n=0;n<=sizeof(ack);++n) {
        delay = 0xfeed;
        const bool match = matchOwnedAck(ack,n,sender,100,300,&delay);
        assert(match == (n==10));
        assert(delay == (match ? 200U : 0xfeedU));
    }
    assert(matchOwnedAck(ack,10,sender,0xfffffff0U,0x20,&delay) && delay==48);
    assert(!matchOwnedAck(ack,10,sender,100,100,&delay));
    assert(!matchOwnedAck(ack,10,sender,100,99,&delay));
    assert(matchOwnedAck(ack,10,sender,100,100100,&delay) && delay==100000);
    assert(!matchOwnedAck(ack,10,sender,100,100101,&delay));
    assert(!matchOwnedAck(nullptr,10,sender,100,300,&delay));
    assert(!matchOwnedAck(ack,10,nullptr,100,300,&delay));
    assert(!matchOwnedAck(ack,10,sender,100,300,nullptr));
    for (unsigned i=4;i<10;++i) {
        ack[i]^=2; assert(!matchOwnedAck(ack,10,sender,100,300,&delay)); ack[i]^=2;
    }
    ack[0]=0xc4; assert(!matchOwnedAck(ack,10,sender,100,300,&delay)); ack[0]=0xd4;
    for (uint8_t flags : {uint8_t(1),uint8_t(2),uint8_t(4),uint8_t(0x40),uint8_t(0x80)}) {
        ack[1]=flags; assert(!matchOwnedAck(ack,10,sender,100,300,&delay));
    }
    ack[1]=0x10; assert(matchOwnedAck(ack,10,sender,100,300,&delay)); ack[1]=0;
    // Independently specified RX header + TX response + SCD next-sequence.
    // Offsets are relative to the full RX buffer, not a packed C++ struct.
    uint8_t buffer[256]={};
    buffer[0]=52; buffer[4]=0x1c; buffer[8]=1;
    buffer[16]=168; buffer[38]=210; buffer[44]=1;
    buffer[48]=1; buffer[52]=1;
    TxCompletionView out={0xaaaa,0xbbbb,0xcccc,0xdddd};
    const auto unchanged=out;
    for (size_t n=0;n<56;++n) {
        assert(!findTxCompletion(buffer,n,1,1,210,&out));
        assert(!memcmp(&out,&unchanged,sizeof(out)));
    }
    assert(findTxCompletion(buffer,56,1,1,210,&out));
    assert(out.status==1 && out.mediaUs==168 && out.byteCount==210 && out.nextSequence==1);
    assert(!findTxCompletion(nullptr,56,1,1,210,&out));
    assert(!findTxCompletion(buffer,8193,1,1,210,&out));
    assert(!findTxCompletion(buffer,56,1,4096,210,&out));
    assert(!findTxCompletion(buffer,56,1,1,0,&out));
    assert(!findTxCompletion(buffer,56,1,1,210,nullptr));
    for (unsigned offset : {4U,5U,8U,38U,44U,52U}) {
        const auto saved=buffer[offset]; buffer[offset]^=2;
        assert(!findTxCompletion(buffer,56,1,1,210,&out)); buffer[offset]=saved;
    }
    buffer[48]=0x83;
    assert(findTxCompletion(buffer,56,1,1,210,&out) && out.status==0x83);
    buffer[52]=0; // scheduler wrap
    assert(findTxCompletion(buffer,56,1,0,210,&out) && out.nextSequence==0);
    // Skip an unrelated completed record at the 64-byte transport alignment.
    memmove(buffer+64,buffer,56); memset(buffer,0,64); buffer[0]=4; buffer[4]=0x42;
    assert(findTxCompletion(buffer,120,1,0,210,&out));
    buffer[0]=0; buffer[1]=0; buffer[2]=0x55; buffer[3]=0x55;
    assert(!findTxCompletion(buffer,120,1,0,210,&out));
    buffer[0]=0xff; buffer[1]=0x3f; buffer[2]=buffer[3]=0;
    assert(!findTxCompletion(buffer,120,1,0,210,&out));
    uint32_t random=0x7162bc41;
    for (unsigned i=0;i<200000;++i) {
        random^=random<<13; random^=random>>17; random^=random<<5;
        buffer[(random>>16)%sizeof(buffer)]=uint8_t(random);
        const auto n=size_t(random%(sizeof(buffer)+1));
        out=unchanged;
        if (!findTxCompletion(buffer,n,1,1,210,&out))
            assert(!memcmp(&out,&unchanged,sizeof(out)));
        else assert(out.byteCount==210 && out.nextSequence==1);
    }
    puts("TX completion bounds, alignment, identity, failure and mutation checks passed");
}
