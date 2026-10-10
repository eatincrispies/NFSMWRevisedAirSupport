#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SoundAI.hpp"

namespace Memory {

    namespace {

        int SafeCopy(void* destination, const void* source, size_t length) {
            __try {
                std::memcpy(destination, source, length);
                return 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

    }

    bool Read(uintptr_t va, void* out, size_t length) {
        return SafeCopy(out, reinterpret_cast<const void*>(va), length) != 0;
    }

    bool Matches(uintptr_t va, const void* expected, size_t length) {
        uint8_t bytes[64] = {};
        return length <= sizeof(bytes) && Read(va, bytes, length) && std::memcmp(bytes, expected, length) == 0;
    }

    bool Write(uintptr_t va, const void* bytes, size_t length) {
        void* target = reinterpret_cast<void*>(va);
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(target, bytes, length);
        FlushInstructionCache(GetCurrentProcess(), target, length);
        DWORD unused = 0;
        VirtualProtect(target, length, oldProtect, &unused);
        return true;
    }

}

namespace Patch {

    namespace {

        constexpr int     kMostPatches = 32;
        constexpr uint8_t kCall        = 0xE8;
        constexpr uint8_t kJump        = 0xE9;
        constexpr uint8_t kNop         = 0x90;

        struct Original {
            uintptr_t va;
            size_t    length;
            uint8_t   bytes[16];
        };

        Original gOriginals[kMostPatches] = {};
        int      gCount = 0;

        uint32_t BranchOffset(uintptr_t from, uintptr_t to) {
            return static_cast<uint32_t>(to) - static_cast<uint32_t>(from + 5);
        }

    }

    bool Replace(uintptr_t va, const void* expected, const void* replacement, size_t length) {
        if (gCount >= kMostPatches) return false;
        Original& original = gOriginals[gCount];
        if (length > sizeof(original.bytes) || !Memory::Read(va, original.bytes, length) || std::memcmp(original.bytes, expected, length) != 0)
            return false;
        if (!Memory::Write(va, replacement, length)) return false;
        original.va = va;
        original.length = length;
        ++gCount;
        return true;
    }

    bool ReplaceVirtual(uintptr_t vtable, unsigned slot, uintptr_t function, const void* replacement) {
        const uint32_t expected = static_cast<uint32_t>(function);
        const uint32_t hook = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement));
        return Replace(vtable + slot, &expected, &hook, sizeof(hook));
    }

    bool RedirectCall(uintptr_t call, uintptr_t function, const void* replacement) {
        uint8_t expected[5] = { kCall };
        uint8_t hook[5] = { kCall };
        const uint32_t expectedOffset = BranchOffset(call, function);
        const uint32_t hookOffset = BranchOffset(call, reinterpret_cast<uintptr_t>(replacement));
        std::memcpy(expected + 1, &expectedOffset, sizeof(expectedOffset));
        std::memcpy(hook + 1, &hookOffset, sizeof(hookOffset));
        return Replace(call, expected, hook, sizeof(hook));
    }

    bool Detour(uintptr_t function, const uint8_t* code, size_t length, const void* replacement, uintptr_t& original) {
        auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline) return false;
        const uint32_t back = BranchOffset(reinterpret_cast<uintptr_t>(trampoline) + length, function + length);
        std::memcpy(trampoline, code, length);
        trampoline[length] = kJump;
        std::memcpy(trampoline + length + 1, &back, sizeof(back));
        FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5);

        uint8_t jump[16] = { kJump };
        const uint32_t offset = BranchOffset(function, reinterpret_cast<uintptr_t>(replacement));
        std::memcpy(jump + 1, &offset, sizeof(offset));
        for (size_t i = 5; i < length; ++i) jump[i] = kNop;
        const uintptr_t previous = original;
        original = reinterpret_cast<uintptr_t>(trampoline);
        if (Replace(function, code, jump, length)) return true;
        original = previous;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    void RestoreAll() {
        while (gCount > 0) {
            const Original& original = gOriginals[--gCount];
            Memory::Write(original.va, original.bytes, original.length);
        }
    }

}

namespace Game {

    template <typename Return, typename... Args>
    Return ThisCall(uintptr_t function, Args... args) {
        return reinterpret_cast<Return (__thiscall*)(Args...)>(function)(args...);
    }

    template <typename Return, typename... Args>
    Return Call(uintptr_t function, Args... args) {
        return reinterpret_cast<Return (__cdecl*)(Args...)>(function)(args...);
    }

    template <typename Method>
    const void* MethodAddress(Method method) {
        static_assert(sizeof(method) == sizeof(void*), "only plain member functions have a single code address");
        const void* address = nullptr;
        std::memcpy(&address, &method, sizeof(address));
        return address;
    }

    template <typename T>
    T& Global(uintptr_t va) {
        return *reinterpret_cast<T*>(va);
    }

}

namespace {

    constexpr uintptr_t kSingleton_SoundAI_mInstance        = 0x00993CC8u;
    constexpr uintptr_t kManager_m_SpeechModule             = 0x0099222Cu;
    constexpr uintptr_t kManager_ScheduleSpeechPartII       = 0x00713B20u;
    constexpr uintptr_t kMiscSpeech_LostSuspect             = 0x0071D960u;
    constexpr uintptr_t kEAXCop_IntentToRam                 = 0x00718A30u;
    constexpr uintptr_t kEAXCop_RegainVisual                = 0x00718C50u;
    constexpr uintptr_t kEAXCharacter_InterruptViolent      = 0x00716F90u;
    constexpr uintptr_t kEAXCop_VehicleReport               = 0x00718150u;
    constexpr uintptr_t kEAXCop_SpotterWanted               = 0x00719A90u;
    constexpr uintptr_t kEAXAirSupport_Update               = 0x00709F50u;
    constexpr uintptr_t kEAXAirSupport_Bailout              = 0x00717C00u;
    constexpr uintptr_t kEventHistory_Find                  = 0x004CB3F0u;
    constexpr uintptr_t kManager_ClearPlayback              = 0x0070E570u;
    constexpr uintptr_t kVehicleReport_speed_test           = 0x008B2660u;
    constexpr uintptr_t kFEDatabase                         = 0x0091CF90u;
    constexpr uintptr_t kScheduledSpeechEvent_GetData       = 0x004B1D60u;
    constexpr uintptr_t kEventHistory_GetCount              = 0x004CB460u;
    constexpr uintptr_t kManager_mGlobalHistory             = 0x00992718u;
    constexpr uintptr_t kManager_IndirectSpeechEvent        = 0x006FEF90u;
    constexpr uint8_t   kIndirectSpeechEventEntry[5]        = { 0xA1, 0x00, 0x87, 0x8F, 0x00 };
    constexpr uintptr_t kManager_IsQueued                   = 0x00709D00u;
    constexpr uintptr_t kManager_IsCopSpeechPlaying         = 0x006FF320u;
    constexpr uintptr_t kISimable_FindInstance              = 0x0041AD40u;
    constexpr uintptr_t kSoundAI_OnCollision                = 0x0071B990u;
    constexpr uint8_t   kSoundAIOnCollisionEntry[6]         = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr uintptr_t kEAXCharacter_SetHandle             = 0x007001D0u;
    constexpr uintptr_t kEAXCharacter_SetLOS                = 0x00700300u;
    constexpr uintptr_t kSoundAI_TerminatePursuit           = 0x0071F6E0u;
    constexpr uint8_t   kTerminatePursuitEntry[8]           = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08, 0x48 };
    constexpr uintptr_t kTerminatePursuitLostSuspectCall    = 0x0071F758u;
    constexpr uintptr_t kSoundAI_ResetPursuit               = 0x0071CB10u;
    constexpr uint8_t   kResetPursuitEntry[10]              = { 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0xFC, 0x01, 0x00, 0x00 };
    constexpr uintptr_t kSoundAI_AddNewHeli                 = 0x0070DA60u;
    constexpr uintptr_t kSyncCarsToActorsAddNewHeliCall     = 0x007213EBu;
    constexpr uintptr_t kAIActionHeliPursuit_SkidHitPursuit = 0x00412B40u;
    constexpr uint8_t   kSkidHitPursuitEntry[7]             = { 0x83, 0xEC, 0x70, 0x53, 0x56, 0x8B, 0xF1 };
    constexpr uintptr_t kEAXCop_CallForRB                   = 0x007196C0u;
    constexpr uintptr_t kSoundAI_GetRoadblock               = 0x00704E00u;
    constexpr uintptr_t kRoadblockFlow_Setup                = 0x0071EF50u;
    constexpr uintptr_t kRoadblockFlowServiceSetupCall      = 0x0071F486u;
    constexpr uintptr_t kManager_PostValidate               = 0x0070C600u;
    constexpr uint8_t   kPostValidateEntry[6]               = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr unsigned  kDepFollowCheck                     = 0x20u;
    constexpr unsigned  kOnScreenOnlyCheck                  = 0x40u;

    constexpr uintptr_t kEAXAirSupportVTable = 0x008B2278u;
    constexpr unsigned  kSetHandleSlot       = 0x2Cu;
    constexpr unsigned  kUpdateSlot          = 0x54u;
    constexpr unsigned  kSetLOSSlot          = 0x74u;
    constexpr unsigned  kBailoutSlot         = 0x104u;
    constexpr unsigned  kIntentToRamSlot     = 0xF8u;

    constexpr int       COPSPEECH_MODULE        = 1;
    constexpr int       kAllEventQueues         = 4;
    constexpr unsigned char kMetricSpeedo       = 1u;
    constexpr int       kSpeedTests             = 11;
    constexpr uintptr_t kSPCH_SampleHeaders     = 0x009C2C10u;
    constexpr uintptr_t kSPCH_SampleHeaderCount = 0x009C2C1Cu;
    constexpr uint32_t  kMostSampleHeaders      = 4096u;
    constexpr unsigned  kSampleEntrySize        = 0x08u;
    constexpr unsigned  kSampleEntryHeader      = 0x04u;
    constexpr unsigned  kHeaderDimensions       = 0x04u;
    constexpr unsigned  kHeaderTakes            = 0x05u;
    constexpr unsigned  kHeaderTakeList         = 0x0Cu;
    constexpr uint8_t   kDimensionMask          = 0x7Fu;
    constexpr unsigned  kEventDataSize          = 0x4000u;

    constexpr uint8_t  kUnusedTag     = 2u;
    constexpr int      kUnusedFlag    = 1 << kUnusedTag;
    constexpr unsigned kConditionMask = 8u;
    constexpr uint32_t kGameFlags     = Csis::Type_intensity_Normal | Csis::Type_intensity_High;
    constexpr uint32_t kAllFlags      = kGameFlags | kUnusedFlag;

    struct UnusedTakes {
        uint16_t sample;
        uint8_t  gameTag;
        unsigned count;
        unsigned takes[4];
        uint8_t  condition[16];
    };

    constexpr UnusedTakes kPullBackTakes = { 0xB1u, 0u, 2u, { 0u, 1u },
                                             { 0xB1, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0x00 } };

    struct SpeechEvent {
        const char* name;
        uintptr_t   interfaceId;
        uint32_t    interfaceCrcs;
        uintptr_t   functionHandle;
        uintptr_t   function;
        unsigned    scheduleOffset;

        Csis::InterfaceId& Id() const {
            return Game::Global<Csis::InterfaceId>(interfaceId);
        }

        Csis::FunctionHandle& Handle() const {
            return Game::Global<Csis::FunctionHandle>(functionHandle);
        }

        bool Verify() const;
    };

    constexpr SpeechEvent kAnytimeEvents_LostSuspect = { "AnytimeEvents_LostSuspect", 0x00901E1Cu, 0x34A45BA7u, 0x009924ECu, kMiscSpeech_LostSuspect, 0x6Fu };
    constexpr SpeechEvent kAnytimeEvents_IntentToRam = { "AnytimeEvents_IntentToRam", 0x00901E5Cu, 0x6C5E5BA7u, 0x00992544u, kEAXCop_IntentToRam,     0x43u };
    constexpr SpeechEvent kAnytimeEvents_RegainVisual  = { "AnytimeEvents_RegainVisual",  0x00901E14u, 0x51D85BA7u, 0x0099240Cu, kEAXCop_RegainVisual,           0x43u };
    constexpr SpeechEvent kInterrupts_InterruptRamHigh = { "Interrupts_InterruptRamHigh", 0x00901F4Cu, 0x3FD55BA7u, 0x009925ACu, kEAXCharacter_InterruptViolent, 0x05u };
    constexpr SpeechEvent kSetup_VehicleReport         = { "Setup_VehicleReport",         0x00901C1Cu, 0x466D5BA7u, 0x0099246Cu, kEAXCop_VehicleReport,          0x1C4u };
    constexpr SpeechEvent kSetup_SpotterWanted         = { "Setup_SpotterWanted",         0x00901BDCu, 0x47C75BA7u, 0x00992314u, kEAXCop_SpotterWanted,          0x05u };
    constexpr SpeechEvent kHeliSpecific_HeliBailout    = { "HeliSpecific_HeliBailout",    0x00901EDCu, 0x23945BA7u, 0x009924D4u, kEAXAirSupport_Bailout,         0x13u };
    constexpr SpeechEvent kStaticRoadblock_CallForRB   = { "StaticRoadblock_CallForRB",   0x00901CD4u, 0x7D0E5BA7u, 0x00992594u, kEAXCop_CallForRB,              0x32u };

    static_assert(offsetof(EAXCharacter, mSpeakerID) == 0x0C, "EAXCharacter::mSpeakerID");
    static_assert(offsetof(EAXCharacter, mSuspectLOS) == 0x36, "EAXCharacter::mSuspectLOS");
    static_assert(offsetof(SoundAI, mDispatch) == 0xD8, "SoundAI::mDispatch");
    static_assert(offsetof(SoundAI, mHeli) == 0xE0, "SoundAI::mHeli");
    static_assert(offsetof(SoundAI, mNumActiveCopCars) == 0x1F0, "SoundAI::mNumActiveCopCars");
    static_assert(offsetof(AIActionHeliPursuit, mIRigidBody) == 0x54, "AIActionHeliPursuit::mIRigidBody");
    static_assert(offsetof(AIActionHeliPursuit, mSkidKnockTimer) == 0x64, "AIActionHeliPursuit::mSkidKnockTimer");
    static_assert(offsetof(AIActionHeliPursuit, mPlayerPosition) == 0x84, "AIActionHeliPursuit::mPlayerPosition");
    static_assert(offsetof(AIActionHeliPursuit, mPursuitMode) == 0xA4, "AIActionHeliPursuit::mPursuitMode");
    static_assert(offsetof(Sim::Collision::Info, closingVel) == 0x20, "Sim::Collision::Info::closingVel");
    static_assert(offsetof(SoundAI, mPlayerSpeed) == 0x108, "SoundAI::mPlayerSpeed");
    static_assert(offsetof(SoundAI, mPursuitState) == 0x1D4, "SoundAI::mPursuitState");
    static_assert(offsetof(SoundAI, mFlags) == 0x54, "SoundAI::mFlags");
    static_assert(offsetof(SoundAI, mFocus) == 0x138, "SoundAI::mFocus");
    static_assert(offsetof(SoundAI, mPVehicle) == 0x168, "SoundAI::mPVehicle");
    static_assert(offsetof(SoundAI, mPlayerCarCustom) == 0x230, "SoundAI::mPlayerCarCustom");
    static_assert(offsetof(Attrib::Gen::pvehicle, mLayoutPtr) == 0x08, "Attrib::Gen::pvehicle::mLayoutPtr");
    static_assert(offsetof(Attrib::Gen::pvehicle::_LayoutStruct, VerbalType) == 0x40, "Attrib::Gen::pvehicle::VerbalType");
    static_assert(offsetof(SoundAI, mNumActiveCopCars) == 0x1F0, "SoundAI::mNumActiveCopCars");
    static_assert(sizeof(Speech::ScheduledSpeechEvent) == 0x40, "Speech::ScheduledSpeechEvent is 0x40 bytes");
    static_assert(offsetof(Speech::ScheduledSpeechEvent, ID) == 0x08, "Speech::ScheduledSpeechEvent::ID");
    static_assert(offsetof(Speech::ScheduledSpeechEvent, actor) == 0x0C, "Speech::ScheduledSpeechEvent::actor");
    static_assert(sizeof(Csis::Setup_VehicleReportStruct) == 0x14, "Csis::Setup_VehicleReportStruct is 0x14 bytes");
    static_assert(sizeof(Csis::StaticRoadblock_CallForRBStruct) == 0x0C, "Csis::StaticRoadblock_CallForRBStruct is 0x0C bytes");
    static_assert(offsetof(Speech::RoadblockFlow, mT_setup) == 0x10, "Speech::RoadblockFlow::mT_setup");
    static_assert(offsetof(UserProfile, TheGameplaySettings) + offsetof(GameplaySettings, SpeedoUnits) == 0x3B, "GameplaySettings::SpeedoUnits");
    static_assert(offsetof(cFrontendDatabase, CurrentUserProfiles) == 0x10, "cFrontendDatabase::CurrentUserProfiles");
    static_assert(offsetof(Sim::Collision::Info, objA) == 0x3C, "Sim::Collision::Info::objA");
    static_assert(offsetof(Sim::Collision::Info, objB) == 0x4C, "Sim::Collision::Info::objB");

    uintptr_t gTerminatePursuitOriginal = kSoundAI_TerminatePursuit;
    uintptr_t gResetPursuitOriginal     = kSoundAI_ResetPursuit;
    uintptr_t gSoundAIOnCollisionOriginal = kSoundAI_OnCollision;
    uintptr_t gIndirectSpeechEventOriginal = kManager_IndirectSpeechEvent;
    uintptr_t gSkidHitPursuitOriginal   = kAIActionHeliPursuit_SkidHitPursuit;
    uintptr_t gPostValidateOriginal     = kManager_PostValidate;

    bool                 gUseUnusedRamTakesNext = true;
    bool                 gPilotSearching        = false;
    int                  gHelisThisPursuit      = 0;
    bool                 gRespawnedHeliSpotting = false;
    bool                 gFirstHeliSpotting     = false;
    bool                 gSpotterWantedOn       = false;
    bool                 gKillShotSaid          = false;
    Timer                gRoadblockSetupHeard   = { -1 };

    struct DescribedCar {
        Csis::Type_car_type  car_type;
        Csis::Type_car_color car_color;
    };

    constexpr int kMostEscapedCars = 8;

    DescribedCar gEscapedCars[kMostEscapedCars] = {};
    int          gEscapedCarCount               = 0;
    int          gNextEscapedCar                = 0;
    Csis::Type_intensity gSearchIntensity       = Csis::Type_intensity_High;

}

Speech::Module* (&Speech::Manager::m_SpeechModule)[2] = Game::Global<Speech::Module* [2]>(kManager_m_SpeechModule);
Speech::EventHistory& Speech::Manager::mGlobalHistory  = Game::Global<Speech::EventHistory>(kManager_mGlobalHistory);
cFrontendDatabase*&   FEDatabase                       = Game::Global<cFrontendDatabase*>(kFEDatabase);

void* Speech::ScheduledSpeechEvent::GetData(unsigned int* datasize) {
    return Game::ThisCall<void*>(kScheduledSpeechEvent_GetData, this, datasize);
}

Speech::History* Speech::EventHistory::Find(SPCHType_1_EventID id) {
    return Game::ThisCall<History*>(kEventHistory_Find, this, id);
}

void Speech::Manager::ClearPlayback() {
    Game::Call<void>(kManager_ClearPlayback);
}

void EAXAirSupport::Update() {
    Game::ThisCall<void>(kEAXAirSupport_Update, this);
}

void EAXAirSupport::Bailout() {
    Game::ThisCall<void>(kEAXAirSupport_Bailout, this);
}

int Speech::EventHistory::GetCount(SPCHType_1_EventID id) {
    return Game::ThisCall<int>(kEventHistory_GetCount, this, id);
}

Csis::Result Speech::Manager::IndirectSpeechEvent(ScheduledSpeechEvent* evt, bool test_only) {
    return Game::Call<Csis::Result>(gIndirectSpeechEventOriginal, evt, test_only);
}

SpeechValRtnType Speech::Manager::PostValidate(ScheduledSpeechEvent* evt, unsigned int mask) {
    return Game::Call<SpeechValRtnType>(gPostValidateOriginal, evt, mask);
}

void Speech::RoadblockFlow::Setup() {
    Game::ThisCall<void>(kRoadblockFlow_Setup, this);
}

IRoadBlock* SoundAI::GetRoadblock() {
    return Game::ThisCall<IRoadBlock*>(kSoundAI_GetRoadblock, this);
}

void EAXCop::VehicleReport() {
    Game::ThisCall<void>(kEAXCop_VehicleReport, this);
}

void EAXCop::SpotterWanted() {
    Game::ThisCall<void>(kEAXCop_SpotterWanted, this);
}

Speech::ScheduledSpeechEvent* Speech::Manager::ScheduleSpeechPartII(unsigned int size, void* data, Csis::InterfaceId& iid, Csis::FunctionHandle& fh,
                                                                    EAXCharacter* actor) {
    return Game::Call<ScheduledSpeechEvent*, unsigned int, void*, Csis::InterfaceId&, Csis::FunctionHandle&, EAXCharacter*>(
        kManager_ScheduleSpeechPartII, size, data, iid, fh, actor);
}

bool Speech::Manager::IsQueued(SPCHType_1_EventID evtID, int indices) {
    return Game::Call<bool>(kManager_IsQueued, evtID, indices);
}

bool Speech::Manager::IsCopSpeechPlaying(SPCHType_1_EventID event_id) {
    return Game::Call<bool>(kManager_IsCopSpeechPlaying, event_id);
}

ISimable* ISimable::FindInstance(HSIMABLE handle) {
    return handle != nullptr ? Game::Call<ISimable*>(kISimable_FindInstance, handle) : nullptr;
}

void Sim::Collision::IListener::OnCollision(const Info& cinfo) {
    Game::ThisCall<void>(gSoundAIOnCollisionOriginal, this, &cinfo);
}

void EAXCharacter::InterruptViolent() {
    Game::ThisCall<void>(kEAXCharacter_InterruptViolent, this);
}

SoundAI* SoundAI::Get() {
    return Game::Global<SoundAI*>(kSingleton_SoundAI_mInstance);
}

void SoundAI::TerminatePursuit(BailoutType type) {
    Game::ThisCall<void>(gTerminatePursuitOriginal, this, type);
}

void SoundAI::ResetPursuit(bool including_music) {
    Game::ThisCall<void>(gResetPursuitOriginal, this, including_music);
}

void SoundAI::AddNewHeli(IVehicle* heli) {
    Game::ThisCall<void>(kSoundAI_AddNewHeli, this, heli);
}

void EAXAirSupport::SetHandle(HSIMABLE handle) {
    Game::ThisCall<void>(kEAXCharacter_SetHandle, this, handle);
}

void EAXAirSupport::SetLOS(bool yes) {
    Game::ThisCall<void>(kEAXCharacter_SetLOS, this, yes);
}

int MiscSpeech::LostSuspect(int spkrID) {
    return Game::Call<int>(kMiscSpeech_LostSuspect, spkrID);
}

void EAXCop::IntentToRam() {
    Game::ThisCall<void>(kEAXCop_IntentToRam, this);
}

void AIActionHeliPursuit::SkidHitPursuit() {
    Game::ThisCall<void>(gSkidHitPursuitOriginal, this);
}

namespace {

    bool SpeechEvent::Verify() const {
        uint32_t id[2] = {};
        char text[48] = "";
        const size_t length = std::strlen(name) + 1;
        if (length > sizeof(text) || !Memory::Read(interfaceId, id, sizeof(id)) || id[1] != interfaceCrcs || !Memory::Read(id[0], text, length)
            || std::memcmp(text, name, length) != 0)
            return false;
        if (function == 0) return true;
        constexpr uint8_t kPush = 0x68;
        uint8_t schedule[10] = { kPush, 0, 0, 0, 0, kPush };
        const uint32_t handleAddress = static_cast<uint32_t>(functionHandle);
        const uint32_t idAddress = static_cast<uint32_t>(interfaceId);
        std::memcpy(schedule + 1, &handleAddress, sizeof(handleAddress));
        std::memcpy(schedule + 6, &idAddress, sizeof(idAddress));
        return Memory::Matches(function + scheduleOffset, schedule, sizeof(schedule));
    }

    bool IsQueuedOrPlaying(SPCHType_1_EventID id) {
        return Speech::Manager::IsQueued(id, kAllEventQueues) || Speech::Manager::IsCopSpeechPlaying(id);
    }

    bool DescribePlayerCar(SoundAI* copspeech, DescribedCar& car) {
        const unsigned int color = copspeech->GetPlayerCarColor();
        if (color == 0 || !copspeech->GetPlayerSpecs().IsValid()) return false;
        car = { copspeech->GetPlayerSpecs().VerbalType(), static_cast<Csis::Type_car_color>(color) };
        return true;
    }

    int FindEscapedCar(const DescribedCar& car) {
        for (int i = 0; i < gEscapedCarCount; ++i)
            if (gEscapedCars[i].car_type == car.car_type && gEscapedCars[i].car_color == car.car_color) return i;
        return -1;
    }

    void RememberEscapedCar(const DescribedCar& car) {
        if (FindEscapedCar(car) >= 0) return;
        gEscapedCars[gNextEscapedCar] = car;
        gNextEscapedCar = (gNextEscapedCar + 1) % kMostEscapedCars;
        if (gEscapedCarCount < kMostEscapedCars) ++gEscapedCarCount;
    }

    void ForgetEscapedCar(const DescribedCar& car) {
        const int found = FindEscapedCar(car);
        if (found < 0) return;
        gEscapedCars[found] = gEscapedCars[gEscapedCarCount - 1];
        --gEscapedCarCount;
        gNextEscapedCar = gEscapedCarCount % kMostEscapedCars;
    }

    bool IsEscapedPlayerCar() {
        SoundAI* copspeech = SoundAI::Get();
        DescribedCar car = {};
        return copspeech != nullptr && DescribePlayerCar(copspeech, car) && FindEscapedCar(car) >= 0;
    }

    void HelicopterArrived() {
        gKillShotSaid = false;
        if (gHelisThisPursuit++ > 0)
            gRespawnedHeliSpotting = true;
        else
            gFirstHeliSpotting = true;
    }

    void ReportLiveSpeed(Csis::Setup_VehicleReportStruct& data, float playerSpeed) {
        const float (&speed_test)[kSpeedTests] = Game::Global<const float[kSpeedTests]>(kVehicleReport_speed_test);
        float speedo = playerSpeed;
        if (FEDatabase != nullptr && FEDatabase->GetGameplaySettings()->SpeedoUnits == kMetricSpeedo) {
            data.measurement = Csis::Type_measurement_metric_only;
            speedo = speedo * 1.60931f;
        } else {
            data.measurement = Csis::Type_measurement_imperial_only;
        }
        int ndx = 0;
        while (ndx < kSpeedTests && !(speedo < speed_test[ndx])) ++ndx;
        if (ndx == 0) {
            data.speed = Csis::Type_speed_over_speed_limit;
            data.measurement = Csis::Type_measurement_generic;
        } else {
            data.speed = static_cast<Csis::Type_speed>(2 << (ndx - 1));
        }
    }

    template <typename T> void ScheduleSpeech(T& data, const SpeechEvent& event, EAXCharacter* actor) {
        Speech::Manager::ScheduleSpeechPartII(sizeof(T), &data, event.Id(), event.Handle(), actor);
    }

    Csis::Type_intensity NextIntensity(Csis::Type_intensity& last) {
        last = last == Csis::Type_intensity_Normal ? Csis::Type_intensity_High : Csis::Type_intensity_Normal;
        return last;
    }

    uint8_t* FindTakeTag(uint16_t sample, uint16_t speaker, unsigned take) {
        uint32_t table = 0;
        uint32_t count = 0;
        if (!Memory::Read(kSPCH_SampleHeaders, &table, sizeof(table)) || !table || !Memory::Read(kSPCH_SampleHeaderCount, &count, sizeof(count))
            || count > kMostSampleHeaders)
            return nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t header = 0;
            uint16_t id[2] = {};
            uint8_t shape[2] = {};
            if (!Memory::Read(table + i * kSampleEntrySize + kSampleEntryHeader, &header, sizeof(header)) || !header
                || !Memory::Read(header, id, sizeof(id)) || id[0] != sample || id[1] != speaker
                || !Memory::Read(header + kHeaderDimensions, shape, sizeof(shape)))
                continue;
            const unsigned dimensions = shape[0] & kDimensionMask;
            if (dimensions == 0 || take >= shape[kHeaderTakes - kHeaderDimensions]) return nullptr;
            return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(header + kHeaderTakeList + (2u + dimensions) * take + 2u));
        }
        return nullptr;
    }

    uint32_t* FindConditionMask(const UnusedTakes& group) {
        Speech::Module* copSpeech = Speech::Manager::m_SpeechModule[COPSPEECH_MODULE];
        char* events = copSpeech != nullptr ? copSpeech->GetEventDat() : nullptr;
        if (!events) return nullptr;
        for (unsigned i = 0; i + sizeof(group.condition) <= kEventDataSize; ++i)
            if (Memory::Matches(reinterpret_cast<uintptr_t>(events) + i, group.condition, sizeof(group.condition)))
                return reinterpret_cast<uint32_t*>(events + i + kConditionMask);
        return nullptr;
    }

    bool TagUnusedTakes(const UnusedTakes& group) {
        for (unsigned i = 0; i < group.count; ++i) {
            uint8_t* tag = FindTakeTag(group.sample, Speech::Heli, group.takes[i]);
            uint8_t current = 0;
            if (!tag || !Memory::Read(reinterpret_cast<uintptr_t>(tag), &current, sizeof(current))) return false;
            if (current == kUnusedTag) continue;
            if (current != group.gameTag || !Patch::Replace(reinterpret_cast<uintptr_t>(tag), &group.gameTag, &kUnusedTag, sizeof(kUnusedTag)))
                return false;
        }
        uint32_t* mask = FindConditionMask(group);
        uint32_t current = 0;
        if (!mask || !Memory::Read(reinterpret_cast<uintptr_t>(mask), &current, sizeof(current))) return false;
        if (current == kAllFlags) return true;
        return current == kGameFlags && Patch::Replace(reinterpret_cast<uintptr_t>(mask), &kGameFlags, &kAllFlags, sizeof(kAllFlags));
    }

    void ScheduleIntentToRam(EAXAirSupport* heli, int intensity) {
        Csis::AnytimeEvents_IntentToRamStruct data = { heli->GetSpeakerID(), static_cast<Csis::Type_intensity>(intensity) };
        ScheduleSpeech(data, kAnytimeEvents_IntentToRam, heli);
    }

    void PilotCallsSearchPattern() {
        gPilotSearching = false;
        Csis::AnytimeEvents_LostSuspectStruct data = { Speech::Heli, NextIntensity(gSearchIntensity) };
        ScheduleSpeech(data, kAnytimeEvents_LostSuspect, nullptr);
    }

}

void EAXAirSupport::IntentToRamHook() {
    if (!TagUnusedTakes(kPullBackTakes)) {
        EAXCop::IntentToRam();
        return;
    }
    const bool useUnusedTakes = gUseUnusedRamTakesNext;
    gUseUnusedRamTakesNext = !gUseUnusedRamTakesNext;
    if (useUnusedTakes)
        ScheduleIntentToRam(this, Csis::Type_intensity_Normal);
    else
        EAXCop::IntentToRam();
}

void EAXAirSupport::IntentToRamPullBack() {
    if (TagUnusedTakes(kPullBackTakes)) ScheduleIntentToRam(this, kUnusedFlag);
}

void EAXAirSupport::RegainVisualAfterRespawn() {
    Csis::AnytimeEvents_RegainVisualStruct data = { GetSpeakerID(), Csis::Type_intensity_Normal };
    ScheduleSpeech(data, kAnytimeEvents_RegainVisual, this);
}

void EAXAirSupport::VehicleReportOnFirstSighting() {
    if (Speech::Manager::GetHistory().GetCount(kSPCH1_EventID_VehicleReport) > 0 || IsQueuedOrPlaying(kSPCH1_EventID_VehicleReport)) return;
    EAXCop::VehicleReport();
}

void EAXAirSupport::SpotterWantedDuringCooldown() {
    gRespawnedHeliSpotting = false;
    gFirstHeliSpotting = false;
    if (IsQueuedOrPlaying(kSPCH1_EventID_SpotterWanted)) return;
    EAXCop::SpotterWanted();
}

void EAXAirSupport::SpotterWantedForEscapedCar() {
    if (Speech::Manager::GetHistory().GetCount(kSPCH1_EventID_SpotterWanted) > 0 || IsQueuedOrPlaying(kSPCH1_EventID_SpotterWanted)) return;
    EAXCop::SpotterWanted();
}

void EAXAirSupport::CallForRBAhead(IRoadBlock* roadblock) {
    if (IsQueuedOrPlaying(kSPCH1_EventID_CallForRB)) return;
    Csis::StaticRoadblock_CallForRBStruct data;
    data.speaker_id = GetSpeakerID();
    data.code = GetRandomizedCode();
    data.roadblock_type = roadblock->GetNumSpikeStrips() > 0 ? Csis::Type_roadblock_type_Spikes : Csis::Type_roadblock_type_Roadblock_Generic_;
    ScheduleSpeech(data, kStaticRoadblock_CallForRB, this);
}

void EAXAirSupport::KillShot() {
    gKillShotSaid = true;
    if (Speech::Module* copSpeech = Speech::Manager::m_SpeechModule[COPSPEECH_MODULE]) copSpeech->ReleaseResource();
    Speech::Manager::ClearPlayback();
    if (Speech::History* history = Speech::Manager::GetHistory().Find(kSPCH1_EventID_HeliBailout)) history->count = 0;
    EAXAirSupport::Bailout();
}

void EAXAirSupport::UpdateHook() {
    const bool wasDead = IsDead();
    EAXAirSupport::Update();
    if (IsDead()) {
        if (!wasDead && !gKillShotSaid) KillShot();
        return;
    }
    if (wasDead) HelicopterArrived();
    gKillShotSaid = false;
}

void EAXAirSupport::BailoutHook() {
    if (!gKillShotSaid) EAXAirSupport::Bailout();
}

void EAXAirSupport::SetHandleHook(HSIMABLE handle) {
    const HSIMABLE previous = GetHandle();
    EAXAirSupport::SetHandle(handle);
    if (handle != nullptr && previous != nullptr && handle != previous) HelicopterArrived();
}

void EAXAirSupport::SetLOSHook(bool yes) {
    const bool spotted = yes && !HasLOS();
    EAXAirSupport::SetLOS(yes);
    if (!spotted || !IsActive()) return;
    SoundAI* copspeech = SoundAI::Get();
    const bool inCooldown = copspeech != nullptr && copspeech->GetFocus() == SoundAI::kLost;
    if (gSpotterWantedOn && inCooldown) {
        SpotterWantedDuringCooldown();
    } else if (gRespawnedHeliSpotting) {
        gRespawnedHeliSpotting = false;
        RegainVisualAfterRespawn();
    } else if (gFirstHeliSpotting) {
        gFirstHeliSpotting = false;
        if (gSpotterWantedOn && IsEscapedPlayerCar()) SpotterWantedForEscapedCar();
        VehicleReportOnFirstSighting();
    }
}

void AIActionHeliPursuit::SkidHitPursuitHook() {
    const kPursuitMode before = mPursuitMode;
    SkidHitPursuit();
    if (before != kSkid_Hit_Approach || mPursuitMode != kStraight_Line) return;
    SoundAI* copspeech = SoundAI::Get();
    if (copspeech != nullptr && copspeech->GetHeli() != nullptr) copspeech->GetHeli()->IntentToRamPullBack();
}

void Sim::Collision::IListener::OnCollisionHook(const Info& cinfo) {
    OnCollision(cinfo);
    if (cinfo.type != Info::OBJECT) return;
    SoundAI* copspeech = SoundAI::Get();
    EAXAirSupport* heli = copspeech != nullptr ? copspeech->GetHeli() : nullptr;
    if (heli == nullptr || heli->IsDead() || !heli->IsActive()) return;
    const HSIMABLE heliHandle = heli->GetHandle();
    if (heliHandle == nullptr || (cinfo.objA != heliHandle && cinfo.objB != heliHandle)) return;
    ISimable* other = ISimable::FindInstance(cinfo.objA == heliHandle ? cinfo.objB : cinfo.objA);
    if (other == nullptr || !other->IsPlayer()) return;
    if (IsQueuedOrPlaying(kSPCH1_EventID_InterruptRamHigh)) return;
    heli->EAXCharacter::InterruptViolent();
}

Csis::Result Speech::Manager::IndirectSpeechEventHook(ScheduledSpeechEvent* evt, bool test_only) {
    SoundAI* copspeech = SoundAI::Get();
    if (evt != nullptr && evt->ID == kSPCH1_EventID_VehicleReport && copspeech != nullptr && evt->actor != nullptr && evt->actor == copspeech->GetHeli()) {
        unsigned int size = 0;
        void* data = evt->GetData(&size);
        if (data != nullptr && size >= sizeof(Csis::Setup_VehicleReportStruct))
            ReportLiveSpeed(*static_cast<Csis::Setup_VehicleReportStruct*>(data), copspeech->GetPlayerSpeed());
    }
    return IndirectSpeechEvent(evt, test_only);
}

SpeechValRtnType Speech::Manager::PostValidateHook(ScheduledSpeechEvent* evt, unsigned int mask) {
    SoundAI* copspeech = SoundAI::Get();
    if (evt != nullptr && evt->ID == kSPCH1_EventID_CallForRB && copspeech != nullptr && evt->actor != nullptr && evt->actor == copspeech->GetHeli())
        mask &= ~(kDepFollowCheck | kOnScreenOnlyCheck);
    return PostValidate(evt, mask);
}

void Speech::RoadblockFlow::SetupHook() {
    if (mT_setup.PackedTime != gRoadblockSetupHeard.PackedTime) {
        gRoadblockSetupHeard = mT_setup;
        SoundAI* copspeech = SoundAI::Get();
        EAXAirSupport* heli = copspeech != nullptr ? copspeech->GetHeli() : nullptr;
        IRoadBlock* roadblock = copspeech != nullptr ? copspeech->GetRoadblock() : nullptr;
        if (heli != nullptr && roadblock != nullptr && !heli->IsDead() && heli->IsActive() && heli->HasLOS()) heli->CallForRBAhead(roadblock);
    }
    Setup();
}

void SoundAI::ResetPursuitHook(bool including_music) {
    DescribedCar car = {};
    if (DescribePlayerCar(this, car)) {
        if (mFlags & BUSTED)
            ForgetEscapedCar(car);
        else if (GetFocus() == kLost)
            RememberEscapedCar(car);
    }
    ResetPursuit(including_music);
    gHelisThisPursuit = 0;
    gRespawnedHeliSpotting = false;
    gFirstHeliSpotting = false;
    gKillShotSaid = false;
}

void SoundAI::AddNewHeliHook(IVehicle* heli) {
    AddNewHeli(heli);
    if (GetHeli() != nullptr) HelicopterArrived();
}

void SoundAI::TerminatePursuitHook(BailoutType type) {
    gPilotSearching = type != kForcedBail && GetHeli() != nullptr && GetNumActiveCopCars() == 0;
    TerminatePursuit(type);
    if (gPilotSearching) PilotCallsSearchPattern();
}

int MiscSpeech::LostSuspectHook(int spkrID) {
    if (gPilotSearching) {
        PilotCallsSearchPattern();
        return Speech::Heli;
    }
    return LostSuspect(spkrID == Speech::Heli ? 0 : spkrID);
}

bool SoundAI::Init(void*) {
    const bool intentToRam = kAnytimeEvents_IntentToRam.Verify()
                          && Patch::ReplaceVirtual(kEAXAirSupportVTable, kIntentToRamSlot, kEAXCop_IntentToRam, Game::MethodAddress(&EAXAirSupport::IntentToRamHook));
    const bool pullBack = intentToRam
                       && Patch::Detour(kAIActionHeliPursuit_SkidHitPursuit, kSkidHitPursuitEntry, sizeof(kSkidHitPursuitEntry),
                                        Game::MethodAddress(&AIActionHeliPursuit::SkidHitPursuitHook), gSkidHitPursuitOriginal);
    const bool respawnSpot = kAnytimeEvents_RegainVisual.Verify()
                          && Patch::Detour(kSoundAI_ResetPursuit, kResetPursuitEntry, sizeof(kResetPursuitEntry), Game::MethodAddress(&SoundAI::ResetPursuitHook),
                                           gResetPursuitOriginal)
                          && Patch::RedirectCall(kSyncCarsToActorsAddNewHeliCall, kSoundAI_AddNewHeli, Game::MethodAddress(&SoundAI::AddNewHeliHook))
                          && Patch::ReplaceVirtual(kEAXAirSupportVTable, kSetHandleSlot, kEAXCharacter_SetHandle, Game::MethodAddress(&EAXAirSupport::SetHandleHook))
                          && Patch::ReplaceVirtual(kEAXAirSupportVTable, kSetLOSSlot, kEAXCharacter_SetLOS, Game::MethodAddress(&EAXAirSupport::SetLOSHook));
    const bool beenHit = kInterrupts_InterruptRamHigh.Verify()
                      && Patch::Detour(kSoundAI_OnCollision, kSoundAIOnCollisionEntry, sizeof(kSoundAIOnCollisionEntry),
                                       Game::MethodAddress(&Sim::Collision::IListener::OnCollisionHook), gSoundAIOnCollisionOriginal);
    const bool vehicleReport = respawnSpot && kSetup_VehicleReport.Verify()
                            && Patch::Detour(kManager_IndirectSpeechEvent, kIndirectSpeechEventEntry, sizeof(kIndirectSpeechEventEntry),
                                             reinterpret_cast<const void*>(&Speech::Manager::IndirectSpeechEventHook), gIndirectSpeechEventOriginal);
    gSpotterWantedOn = respawnSpot && kSetup_SpotterWanted.Verify();
    const bool killShot = kHeliSpecific_HeliBailout.Verify()
                       && Patch::ReplaceVirtual(kEAXAirSupportVTable, kBailoutSlot, kEAXAirSupport_Bailout, Game::MethodAddress(&EAXAirSupport::BailoutHook))
                       && Patch::ReplaceVirtual(kEAXAirSupportVTable, kUpdateSlot, kEAXAirSupport_Update, Game::MethodAddress(&EAXAirSupport::UpdateHook));
    const bool roadblockAhead = kStaticRoadblock_CallForRB.Verify()
                             && Patch::Detour(kManager_PostValidate, kPostValidateEntry, sizeof(kPostValidateEntry),
                                              reinterpret_cast<const void*>(&Speech::Manager::PostValidateHook), gPostValidateOriginal)
                             && Patch::RedirectCall(kRoadblockFlowServiceSetupCall, kRoadblockFlow_Setup, Game::MethodAddress(&Speech::RoadblockFlow::SetupHook));
    const bool searchPattern = kAnytimeEvents_LostSuspect.Verify()
                            && Patch::RedirectCall(kTerminatePursuitLostSuspectCall, kMiscSpeech_LostSuspect, reinterpret_cast<const void*>(&MiscSpeech::LostSuspectHook))
                            && Patch::Detour(kSoundAI_TerminatePursuit, kTerminatePursuitEntry, sizeof(kTerminatePursuitEntry),
                                             Game::MethodAddress(&SoundAI::TerminatePursuitHook), gTerminatePursuitOriginal);
    return intentToRam || pullBack || respawnSpot || vehicleReport || beenHit || killShot || roadblockAhead || searchPattern;
}

void SoundAI::Restore() {
    Patch::RestoreAll();
}
