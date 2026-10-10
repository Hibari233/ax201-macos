// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlUserClient.hpp"
#include "AirportItlwmV2.hpp"

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
