// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlTxCompletion.h>
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
using namespace ItlAwdl;

int main() {
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
