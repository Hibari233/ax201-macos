// SPDX-License-Identifier: GPL-2.0-or-later
// Borrowed management-frame buffers, audited on x86_64 Sequoia 24H32.
#pragma once
#include <libkern/c++/OSObject.h>

class IO80211Buffer;

// Use the concrete pool returned by VirtualInterface::getMgmtFramePool().
// The native base get/return/flush/reset implementations panic; never allocate
// this base or make a qualified call which bypasses the concrete virtual slot.
class IO80211BufferPool : public OSObject {
    OSDeclareDefaultStructors(IO80211BufferPool)
public:
    void free() override;
    virtual IO80211Buffer *getBuffer();                      // vptr + 0x118
    virtual void returnBuffer(IO80211Buffer *);             // vptr + 0x120
private:
    virtual void _native_0128() __asm__("__ZN17IO80211BufferPool9flushPoolEv");
    virtual void _native_0130() __asm__("__ZN17IO80211BufferPool9resetPoolEv");
    virtual void _native_0138() const __asm__("__ZNK17IO80211BufferPool10printStatsEPKc");
    virtual void _native_0140() __asm__("__ZN17IO80211BufferPool13mapBufferPoolEv");
    virtual void _native_0148() __asm__("__ZN17IO80211BufferPool15unmapBufferPoolEv");
};
static_assert(sizeof(IO80211BufferPool) == 0x10, "24H32 management pool base");

class IO80211Buffer : public OSObject {
    OSDeclareDefaultStructors(IO80211Buffer)
public:
    void free() override;
    void *getBytesNoCopy();
    unsigned long getCapacity() const;
    unsigned long getLength() const;
    // Native setLength only stores the low 32 bits; it performs no capacity
    // validation. The caller must check both limits before setting it.
    void setLength(unsigned long);
    // Return a lease to its originating pool; OSObject::release is not a
    // substitute. After native bpfTapInput handoff the caller owns no lease.
    void returnBuffer();
private:
    void *opaqueStorage;
};
static_assert(sizeof(IO80211Buffer) == 0x18, "24H32 management buffer");
