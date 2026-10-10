// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOUserClient.h>
#include <AWDL/ItlAwdlABI.h>
class AirportItlwm;
class ItlIwx;
class ItlAwdlUserClient : public IOUserClient {
    OSDeclareDefaultStructors(ItlAwdlUserClient)
public:
    bool initWithTask(task_t, void *, UInt32, OSDictionary *) override;
    bool start(IOService *) override;
    void stop(IOService *) override;
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t, IOExternalMethodArguments *, IOExternalMethodDispatch *, OSObject *, void *) override;
private:
    AirportItlwm *driver;
    ItlIwx *radio;
    static IOReturn status(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn begin(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn end(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn read(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn readRejected(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn probe(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn txStatus(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn regStatus(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn readTimed(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn clock(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn runTest(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn runStatus(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn prepareCountry(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn beginLinked(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn linkedStatus(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn beginData(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn readData(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn dataStatus(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn dataProbe(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeInit(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeStart(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeSession(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativePackets(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeBSD(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeConfigure(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeActivationPreflight(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeTemplate(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeTiming(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeRadio(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeP2pRadio(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeP2pUnicastTx(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeP2pTxSeries(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeP2pPsfSeries(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeSyncTx(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeUnicastTx(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeTransport(OSObject *, void *, IOExternalMethodArguments *);
    static IOReturn nativeQueues(OSObject *, void *, IOExternalMethodArguments *);
};
