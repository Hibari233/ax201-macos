// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlUserClient.hpp"
#include "AirportItlwmV2.hpp"
#include "ItlAwdlNativeInterface.hpp"

OSDefineMetaClassAndStructors(ItlAwdlUserClient, IOUserClient)

bool ItlAwdlUserClient::initWithTask(task_t task, void *token, UInt32 type, OSDictionary *properties) {
    if (type != ITL_AWDL_USER_CLIENT ||
        clientHasPrivilege(task, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return false;
    if (!IOUserClient::initWithTask(task, token, type, properties)) return false;
    setProperty("IOUserClientDefaultLocking", kOSBooleanTrue);
    return true;
}
bool ItlAwdlUserClient::start(IOService *provider) {
    auto candidate = OSDynamicCast(AirportItlwm, provider);
    if (!candidate) return false;
    auto candidateRadio = OSDynamicCast(ItlIwx, candidate->fHalService);
    if (!candidateRadio || !IOUserClient::start(provider)) return false;
    driver = candidate;
    radio = candidateRadio;
    radio->retain();
    return true;
}
void ItlAwdlUserClient::stop(IOService *provider) {
    if (radio) radio->awdlCommand(this, ItlAwdlEnd, nullptr, nullptr);
    OSSafeReleaseNULL(radio);
    driver = nullptr;
    IOUserClient::stop(provider);
}
IOReturn ItlAwdlUserClient::clientClose() {
    if (radio) radio->awdlCommand(this, ItlAwdlEnd, nullptr, nullptr);
    if (!isInactive()) terminate();
    return kIOReturnSuccess;
}
IOReturn ItlAwdlUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *) {
    static const IOExternalMethodDispatch methods[] = {
        {status, 0, 0, 0, sizeof(ItlAwdlStatusResponse)},
        {begin, 0, sizeof(ItlAwdlBeginRequest), 0, 0},
        {end, 0, 0, 0, 0},
        {read, 0, 0, 0, sizeof(ItlAwdlFrameResponse)},
        {readRejected, 0, 0, 0, sizeof(ItlAwdlFrameResponse)},
        {probe, 0, 0, 0, 0},
        {txStatus, 0, 0, 0, sizeof(ItlAwdlTxStatusResponse)},
        {regStatus, 0, 0, 0, sizeof(ItlAwdlRegStatusResponse)},
        {readTimed, 0, 0, 0, sizeof(ItlAwdlTimedFrameResponse)},
        {clock, 0, 0, 0, sizeof(ItlAwdlClockResponse)},
        {runTest, 0, sizeof(ItlAwdlRunRequest), 0, 0},
        {runStatus, 0, 0, 0, sizeof(ItlAwdlRunStatusResponse)},
        {prepareCountry, 0, 0, 0, 0},
        {beginLinked, 0, sizeof(ItlAwdlBeginRequest), 0, 0},
        {linkedStatus, 0, 0, 0, sizeof(ItlAwdlLinkedStatusResponse)},
        {beginData, 0, sizeof(ItlAwdlBeginRequest), 0, 0},
        {readData, 0, 0, 0, sizeof(ItlAwdlDataResponse)},
        {dataStatus, 0, 0, 0, sizeof(ItlAwdlDataStatusResponse)},
        {dataProbe, 0, sizeof(ItlAwdlDataProbeRequest), 0, 0},
        {nativeInit, 0, sizeof(ItlAwdlNativeInitRequest), 0, sizeof(ItlAwdlNativeInitResponse)},
        {nativeStart, 0, sizeof(ItlAwdlNativeStartRequest), 0, sizeof(ItlAwdlNativeStartResponse)},
        {nativeSession, 0, sizeof(ItlAwdlNativeSessionRequest), 0, sizeof(ItlAwdlNativeSessionResponse)},
        {nativePackets, 0, sizeof(ItlAwdlNativePacketRequest), 0, sizeof(ItlAwdlNativePacketResponse)},
        {nativeQueues, 0, sizeof(ItlAwdlNativeQueueRequest), 0, sizeof(ItlAwdlNativeQueueResponse)},
        {nativeTransport, 0, sizeof(ItlAwdlNativeTransportRequest), 0, sizeof(ItlAwdlNativeTransportResponse)},
        {nativeBSD, 0, sizeof(ItlAwdlNativeBSDRequest), 0, sizeof(ItlAwdlNativeBSDResponse)},
        {nativeConfigure, 0, sizeof(ItlAwdlNativeConfigureRequest), 0, sizeof(ItlAwdlNativeConfigureResponse)},
        {nativeTemplate, 0, sizeof(ItlAwdlNativeConfigureRequest), 0, sizeof(ItlAwdlNativeTemplateResponse)},
        {nativeRadio, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeRadioResponse)},
        {nativeSyncTx, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeSyncTxResponse)},
        {nativeTiming, 0, sizeof(ItlAwdlNativeConfigureRequest), 0, sizeof(ItlAwdlNativeTimingResponse)},
        {nativeUnicastTx, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeSyncTxResponse)},
        {nativeP2pRadio, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeRadioResponse)},
        {nativeP2pUnicastTx, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeSyncTxResponse)},
        {nativeP2pTxSeries, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeSyncTxResponse)},
        {nativeP2pPsfSeries, 0, sizeof(ItlAwdlBeginRequest), 0, sizeof(ItlAwdlNativeSyncTxResponse)},
    };
    static_assert(sizeof(methods) / sizeof(methods[0]) == ItlAwdlMethodCount, "dispatch table");
    if (selector >= ItlAwdlMethodCount) return kIOReturnUnsupported;
    if (!args || args->structureInputDescriptor || args->structureOutputDescriptor)
        return kIOReturnBadArgument;
    if (isInactive() || !driver || driver->isInactive() || !radio) return kIOReturnNotReady;
    return IOUserClient::externalMethod(selector, args,
        const_cast<IOExternalMethodDispatch *>(&methods[selector]), this, nullptr);
}
IOReturn ItlAwdlUserClient::status(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::nativeInit(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeInitialization(self->driver, self->radio,
        static_cast<const ItlAwdlNativeInitRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeInitResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativePackets(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probePackets(self->driver, self->radio,
        static_cast<const ItlAwdlNativePacketRequest *>(a->structureInput),
        static_cast<ItlAwdlNativePacketResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeBSD(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::managedBSD(self->driver, self->radio,
        static_cast<const ItlAwdlNativeBSDRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeBSDResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeConfigure(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeConfiguration(self->driver, self->radio,
        static_cast<const ItlAwdlNativeConfigureRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeConfigureResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeTemplate(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeTemplate(self->driver, self->radio,
        static_cast<const ItlAwdlNativeConfigureRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeTemplateResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeTiming(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeTiming(self->driver, self->radio,
        static_cast<const ItlAwdlNativeConfigureRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeTimingResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeRadio(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeRadioResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeSyncTx(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    auto out = static_cast<ItlAwdlNativeSyncTxResponse *>(a->structureOutput);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput), &out->radio, out);
}
IOReturn ItlAwdlUserClient::nativeP2pRadio(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeRadioResponse *>(a->structureOutput), nullptr, false, true);
}
IOReturn ItlAwdlUserClient::nativeUnicastTx(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    auto out = static_cast<ItlAwdlNativeSyncTxResponse *>(a->structureOutput);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput), &out->radio, out, true);
}
IOReturn ItlAwdlUserClient::nativeP2pUnicastTx(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    auto out = static_cast<ItlAwdlNativeSyncTxResponse *>(a->structureOutput);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput), &out->radio, out, true, true);
}
IOReturn ItlAwdlUserClient::nativeTransport(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::managedTransport(self->driver, self->radio,
        static_cast<const ItlAwdlNativeTransportRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeTransportResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeP2pTxSeries(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    auto out = static_cast<ItlAwdlNativeSyncTxResponse *>(a->structureOutput);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput), &out->radio, out, true, true, true);
}
IOReturn ItlAwdlUserClient::nativeP2pPsfSeries(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    auto out = static_cast<ItlAwdlNativeSyncTxResponse *>(a->structureOutput);
    return ItlAwdlNativeInterface::probeRadio(self->driver, self->radio,
        static_cast<const ItlAwdlBeginRequest *>(a->structureInput), &out->radio, out, false, true, true);
}
IOReturn ItlAwdlUserClient::nativeQueues(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeQueues(self->driver, self->radio,
        static_cast<const ItlAwdlNativeQueueRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeQueueResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeStart(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::probeStart(self->driver, self->radio,
        static_cast<const ItlAwdlNativeStartRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeStartResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::nativeSession(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return ItlAwdlNativeInterface::managedSession(self->driver, self->radio,
        static_cast<const ItlAwdlNativeSessionRequest *>(a->structureInput),
        static_cast<ItlAwdlNativeSessionResponse *>(a->structureOutput));
}
IOReturn ItlAwdlUserClient::begin(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlBegin, a->structureInput, nullptr);
}
IOReturn ItlAwdlUserClient::end(OSObject *o, void *, IOExternalMethodArguments *) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlEnd, nullptr, nullptr);
}
IOReturn ItlAwdlUserClient::read(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlRead, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::readRejected(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlReadRejected, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::probe(OSObject *o, void *, IOExternalMethodArguments *) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlProbe, nullptr, nullptr);
}
IOReturn ItlAwdlUserClient::txStatus(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlTxStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::regStatus(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlRegStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::readTimed(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlReadTimed, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::clock(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlClock, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::runTest(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlRunTest, a->structureInput, nullptr);
}
IOReturn ItlAwdlUserClient::runStatus(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlRunStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::prepareCountry(OSObject *o, void *, IOExternalMethodArguments *) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlPrepareCountry, nullptr, nullptr);
}
IOReturn ItlAwdlUserClient::beginLinked(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlBeginLinked, a->structureInput, nullptr);
}
IOReturn ItlAwdlUserClient::linkedStatus(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlLinkedStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::beginData(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlBeginData, a->structureInput, nullptr);
}
IOReturn ItlAwdlUserClient::readData(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlReadData, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::dataStatus(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlDataStatus, nullptr, a->structureOutput);
}
IOReturn ItlAwdlUserClient::dataProbe(OSObject *o, void *, IOExternalMethodArguments *a) {
    auto self = static_cast<ItlAwdlUserClient *>(o);
    return self->radio->awdlCommand(self, ItlAwdlDataProbe, a->structureInput, nullptr);
}

IOReturn AirportItlwm::newUserClient(task_t task, void *token, UInt32 type,
    OSDictionary *properties, IOUserClient **handler) {
    if (type != ITL_AWDL_USER_CLIENT)
        return IO80211Controller::newUserClient(task, token, type, properties, handler);
    if (!handler) return kIOReturnBadArgument;
    *handler = nullptr;
    if (IOUserClient::clientHasPrivilege(task, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPrivileged;
    if (isInactive() || !OSDynamicCast(ItlIwx, fHalService)) return kIOReturnUnsupported;
    auto client = new ItlAwdlUserClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, token, type, properties)) {
        client->release(); return kIOReturnError;
    }
    if (!client->attach(this)) {
        client->release(); return kIOReturnError;
    }
    if (!client->start(this)) {
        client->detach(this); client->release(); return kIOReturnError;
    }
    *handler = client;
    return kIOReturnSuccess;
}
