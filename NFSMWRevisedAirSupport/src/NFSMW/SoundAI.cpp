#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#if defined(_DEBUG)
#include <algorithm>
#include <string>
#include <vector>
#endif
#include "SoundAI.hpp"

namespace SoundAI {

    namespace {

        constexpr const char* kVersion = "V1.0.0";

        constexpr uintptr_t kSpeechModule    = 0x00993CC8u;
        constexpr unsigned  kSpeakersBegin   = 0x5Cu;
        constexpr unsigned  kSpeakersEnd     = 0x60u;
        constexpr unsigned  kSpeakerSize     = 0x08u;
        constexpr unsigned  kSpeakerVoice    = 0x04u;
        constexpr unsigned  kDispatcher      = 0xD8u;
        constexpr unsigned  kHeliRadio       = 0xE0u;
        constexpr unsigned  kHeatLevel       = 0x104u;
        constexpr unsigned  kSuspectSpeed    = 0x108u;
        constexpr unsigned  kPursuitTime     = 0x140u;
        constexpr unsigned  kPursuitState    = 0x1D4u;
        constexpr uint32_t  kPursuitCooldown = 1u;
        constexpr uintptr_t kHeliVehicle     = 0x0090D61Cu;
        constexpr uintptr_t kPursuitTheme    = 0x009121E8u;
        constexpr uintptr_t kRain            = 0x009196B8u;
        constexpr unsigned  kRainLevel       = 0x28Cu;
        constexpr uintptr_t kGameClock       = 0x00925AE8u;
        constexpr uint32_t  kTicksPerSecond  = 4000u;

        constexpr uintptr_t kHeliRadioVtable  = 0x008B2278u;
        constexpr uintptr_t kCopRadioVtable   = 0x008B1FA0u;
        constexpr unsigned  kRadioVoice       = 0x0Cu;
        constexpr unsigned  kRadioBattalion   = 0x14u;
        constexpr unsigned  kRadioUnitNumber  = 0x18u;
        constexpr unsigned  kRadioVehicleSlot = 0x28u;
        constexpr unsigned  kRadioHealth      = 0x30u;
        constexpr unsigned  kRadioDestroyed   = 0x34u;
        constexpr unsigned  kRadioHasVisual   = 0x35u;
        constexpr unsigned  kRadioIsClose     = 0x36u;
        constexpr float     kFullHealth       = 1.0f;

        constexpr uintptr_t kQueueEvent          = 0x00713B20u;
        constexpr uint8_t   kQueueEventCode[6]   = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
        constexpr unsigned  kEventSpeaker        = 0x0Cu;
        constexpr unsigned  kEventPriority       = 0x3Bu;
        constexpr uint8_t   kLinePriority        = 100u;
        constexpr uint8_t   kInterruptPriority   = 200u;
        constexpr uintptr_t kSpeechQueues        = 0x00993398u;
        constexpr unsigned  kQueueStride         = 0x14u;
        constexpr unsigned  kQueueEnd            = 0x04u;
        constexpr unsigned  kQueueCount          = 4u;
        constexpr uintptr_t kSpeechPlayer        = 0x00992230u;
        constexpr unsigned  kPlayingEvent        = 0x88u;
        constexpr uintptr_t kRadioBusy           = 0x007040C0u;
        constexpr uintptr_t kClosestVoice        = 0x00708390u;
        constexpr uintptr_t kPlayCounts          = 0x00992718u;
        constexpr uintptr_t kFindPlayCount       = 0x004CB3F0u;
        constexpr unsigned  kPlayCount           = 0x04u;
        constexpr uintptr_t kRepeatFilter        = 0x00704490u;
        constexpr uint8_t   kRepeatFilterCode[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
        constexpr uint8_t   kAlwaysRepeat[6]     = { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 };

        constexpr uintptr_t kFindCollection             = 0x00455FD0u;
        constexpr uintptr_t kFindAttribute              = 0x00454810u;
        constexpr uint32_t  kSpeechEventClass           = 0xC593DD47u;
        constexpr uint32_t  kLeadInList                 = 0xC8C5D475u;
        constexpr uint32_t  kNeedsCloseSpeaker          = 0xE0241FC1u;
        constexpr uint32_t  kNeedsSpeakerVehicle        = 0x4B331604u;
        constexpr uint32_t  kCallForBackupAttributes    = 0xA4911F22u;
        constexpr uint32_t  kBackupReplyAttributes      = 0x732FA60Au;
        constexpr uint32_t  kUnit911ReplyAttributes     = 0xC6B1C631u;
        constexpr uint32_t  kCallForRoadblockAttributes = 0x26EF7810u;
        constexpr uint32_t  kRegainVisualAttributes     = 0xFD58F23Du;
        constexpr uint32_t  kHeliBailoutAttributes      = 0x602ACE63u;
        constexpr unsigned  kListHeader                 = 0x08u;
        constexpr unsigned  kListCount                  = 0x02u;
        constexpr unsigned  kListItemSize               = 0x04u;
        constexpr unsigned  kListFlags                  = 0x06u;
        constexpr uint16_t  kListWideHeader             = 0x8000u;
        constexpr uint16_t  kLeadInSize                 = 0x0Cu;
        constexpr uint16_t  kMaxLeadIns                 = 16u;
        constexpr unsigned  kCollectionLayout           = 0x18u;
        constexpr unsigned  kLayoutMaxWait              = 0x0Cu;
        constexpr unsigned  kLayoutDistance             = 0x10u;
        constexpr float     kLongestMaxWait             = 30.0f;
        constexpr float     kRelaxedMaxWait             = 20.0f;
        constexpr float     kLongestDistance            = 100000.0f;
        constexpr float     kRelaxedDistance            = 100000.0f;

        constexpr uintptr_t kSampleHeaders     = 0x009C2C10u;
        constexpr uintptr_t kSampleHeaderCount = 0x009C2C1Cu;
        constexpr unsigned  kSampleEntrySize   = 0x08u;
        constexpr unsigned  kSampleEntryHeader = 0x04u;
        constexpr uint32_t  kMaxSampleHeaders  = 4096u;
        constexpr unsigned  kHeaderDimensions  = 0x04u;
        constexpr unsigned  kHeaderTakeList    = 0x0Cu;
        constexpr uint8_t   kDimensionMask     = 0x7Fu;
        constexpr uint8_t   kUnusedTag         = 7u;
        constexpr uintptr_t kTakeOffsetCall    = 0x00833EEBu;
        constexpr uintptr_t kTakeOffset        = 0x008349FAu;
        constexpr uintptr_t kTakeCheckCall     = 0x0083344Cu;
        constexpr uintptr_t kTakeCheck         = 0x00833087u;

        constexpr uintptr_t kRadioUpdate        = 0x00709F50u;
        constexpr uintptr_t kRadioUpdateSlot    = kHeliRadioVtable + 0x54u;
        constexpr uintptr_t kCopRadioUpdate     = 0x00707BF0u;
        constexpr uintptr_t kCopRadioUpdateSlot = kCopRadioVtable + 0x54u;

        constexpr uintptr_t kSupportRequest        = 0x007048C0u;
        constexpr uint8_t   kSupportRequestCode[7] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x50, 0x10 };
        constexpr unsigned  kRequestType           = 0x10u;
        constexpr uint32_t  kRoadblockRequest      = 0x40u;
        constexpr uint32_t  kStrategyRequest       = 0x10u;

        constexpr uintptr_t kCollisionReaction        = 0x0071BCF5u;
        constexpr uint8_t   kCollisionReactionCode[6] = { 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00 };
        constexpr uintptr_t kBailoutTakes             = 0x00707860u;
        constexpr uintptr_t kBailoutTakesCall         = 0x00717C0Du;
        constexpr uintptr_t kFuelCheck                = 0x00709F75u;
        constexpr uint8_t   kFuelCheckCode[40]        = {
            0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x28, 0x50, 0xE8, 0xBE, 0x0D, 0xD1, 0xFF, 0x8B, 0x48, 0x04, 0x83, 0xC4, 0x04, 0x68,
            0x60, 0x40, 0x40, 0x00, 0xE8, 0x5E, 0xBA, 0xEC, 0xFF, 0x85, 0xC0, 0x74, 0x1E, 0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52, 0x34,
        };
        constexpr uintptr_t kFindVehicle              = 0x0041AD40u;
        constexpr uintptr_t kFindInterface            = 0x005D59F0u;
        constexpr uint32_t  kFuelGauge                = 0x00404060u;
        constexpr unsigned  kVehicleParts             = 0x04u;
        constexpr unsigned  kFuelLeftSlot             = 0x34u;
        constexpr uintptr_t kLostSuspectCall          = 0x0071F758u;

        struct Event {
            const char* name;
            uintptr_t   entry;
            uint32_t    id;
            uintptr_t   binding;
            uintptr_t   function;
            unsigned    pushes;
            uint32_t    key;
        };

        constexpr Event kIntentToRam      = { "AnytimeEvents_IntentToRam",      0x00901E5Cu, 0x6C5E5BA7u, 0x00992544u, 0x00718A30u, 0x43u,  0x71u };
        constexpr Event kStrategyReset    = { "Outcome_StrategyReset",          0x00901D84u, 0x4EA05BA7u, 0x0099242Cu, 0x00718D50u, 0x42u,  0x5Eu };
        constexpr Event kLostSuspect      = { "AnytimeEvents_LostSuspect",      0x00901E1Cu, 0x34A45BA7u, 0x009924ECu, 0x0071D960u, 0x6Fu,  0x6Cu };
        constexpr Event kHeliBailout      = { "HeliSpecific_HeliBailout",       0x00901EDCu, 0x23945BA7u, 0x009924D4u, 0x00717C00u, 0x13u,  0xB0u };
        constexpr Event kHazardAlert      = { "HeliSpecific_HeliHazardAlert",   0x00901EF4u, 0x4AFB5BA7u, 0x0099235Cu, 0x00717CA0u, 0x0Bu,  0xB3u };
        constexpr Event kRegainVisual     = { "AnytimeEvents_RegainVisual",     0x00901E14u, 0x51D85BA7u, 0x0099240Cu, 0x00718C50u, 0x43u,  0x6Bu };
        constexpr Event kSpotted          = { "AnytimeEvents_Spotted",          0x00901E94u, 0x0CB35BA7u, 0x00992524u, 0x007199D0u, 0x30u,  0xD6u };
        constexpr Event kUnit911Reply     = { "AnytimeEvents_Unit911Reply",     0x00901DF4u, 0x383D5BA7u, 0x009926CCu, 0x00718060u, 0x05u,  0x67u };
        constexpr Event kCallForBackup    = { "Backup_CallForBU",               0x00901C84u, 0x398D5BA7u, 0x00992564u, 0x00718640u, 0x1Fu,  0x43u };
        constexpr Event kDispBackupReply  = { "Backup_DispBackupReply",         0x00901C94u, 0x5C2F5BA7u, 0x009926B0u, 0x00717090u, 0x4Au,  0xB8u };
        constexpr Event kDispBUETA        = { "Backup_DispBUETA",               0x00901CA4u, 0x3C9E5BA7u, 0x00992464u, 0x00717860u, 0x12Cu, 0xE4u };
        constexpr Event kHeliSwarming     = { "HeliSpecific_HeliSwarming",      0x00901EE4u, 0x22745BA7u, 0x00992474u, 0x00717C40u, 0x05u,  0xB1u };
        constexpr Event kCallForRoadblock = { "StaticRoadblock_CallForRB",      0x00901CD4u, 0x7D0E5BA7u, 0x00992594u, 0x007196C0u, 0x32u,  0x4Cu };
        constexpr Event kRoadblockUpdate  = { "StaticRoadblock_DispRBUpdate",   0x00901CF4u, 0x7B7F5BA7u, 0x009926F4u, 0x007176D0u, 0x2Eu,  0x4Eu };
        constexpr Event kDriverHistory    = { "AnytimeEvents_DriverHistory",    0x00901E84u, 0x689E5BA7u, 0x0099231Cu, 0x00717020u, 0x07u,  0x76u };
        constexpr Event kPursuitUpdateRep = { "AnytimeEvents_PursuitUpdateRep", 0x00901DD4u, 0x795D5BA7u, 0x009926D8u, 0x007194D0u, 0x05u,  0x64u };
        constexpr Event kSuspectBehaviour = { "AnytimeEvents_SuspectBehaviour", 0x00901E7Cu, 0x688B5BA7u, 0x00993C80u, 0x00718EE0u, 0x23u,  0x74u };
        constexpr Event kPursuitBroadcast = { "AnytimeEvents_DispPursEscGen",   0x00901E3Cu, 0x6BAC5BA7u, 0x0099237Cu, 0x00717190u, 0x23u,  0xB5u };
        constexpr Event kVehicleReport    = { "Setup_VehicleReport",            0x00901C1Cu, 0x466D5BA7u, 0x0099246Cu, 0x00718150u, 0x1C4u, 0x9Bu };
        constexpr Event kWeatherReport    = { "AnytimeEvents_WeatherReport",    0x00901EA4u, 0x7F435BA7u, 0x0099236Cu, 0x00719B00u, 0x05u,  0xDBu };
        constexpr uintptr_t kInitialCallForBU  = 0x00901C74u;
        constexpr uintptr_t kDispPursuitUpdate = 0x00901DCCu;

        constexpr uintptr_t kIntentToRamSlot      = kHeliRadioVtable + 0xF8u;
        constexpr uintptr_t kStrategyResetSlot    = kHeliRadioVtable + 0xC0u;
        constexpr uintptr_t kHeliBailoutSlot      = kHeliRadioVtable + 0x104u;
        constexpr uintptr_t kHazardAlertSlot      = kHeliRadioVtable + 0x214u;
        constexpr uintptr_t kRegainVisualSlot     = kHeliRadioVtable + 0xD8u;
        constexpr uintptr_t kSpottedSlot          = kHeliRadioVtable + 0x140u;
        constexpr uintptr_t kUnit911ReplySlot     = kHeliRadioVtable + 0x120u;
        constexpr uintptr_t kHeliSwarmingSlot     = kHeliRadioVtable + 0x210u;
        constexpr uintptr_t kPursuitUpdateRepSlot = kHeliRadioVtable + 0xE4u;
        constexpr uintptr_t kSuspectBehaviourSlot = kHeliRadioVtable + 0xC4u;
        constexpr uintptr_t kWeatherReportSlot    = kHeliRadioVtable + 0x154u;
        constexpr unsigned  kIntensitySlot        = 0x78u;
        constexpr unsigned  kVehicleReportSlot    = 0x84u;
        constexpr unsigned  kCallForBackupSlot    = 0x98u;
        constexpr unsigned  kDrivingLineSlot      = 0xC4u;
        constexpr unsigned  kCallForRoadblockSlot = 0x158u;

        constexpr uint32_t kHeliVoice          = 2u;
        constexpr uint16_t kDispatcherVoice    = 1u;
        constexpr uint32_t kAirSupportVoices[] = { 3u, 4u, 5u, 9u };
        constexpr uint32_t kNormalTakes        = 1u;
        constexpr uint32_t kHighIntensityTakes = 2u;
        constexpr uint32_t kSingleSuspect      = 1u;
        constexpr uint32_t kMultipleSuspects   = 2u;
        constexpr uint32_t kAirSupport         = 8u;
        constexpr int      kApproved           = 1;
        constexpr uint32_t kTunnelAhead        = 4u;
        constexpr uint32_t kDamagedTakes       = 8u;
        constexpr uint32_t kFuelTakes          = 4u;
        constexpr uint32_t kRoadblockUpdates[][2] = { { 1u, 1u }, { 2u, 2u } };

        constexpr uint16_t kCheckInSample       = 0xC4u;
        constexpr int      kBackupReplyTakes    = 4;
        constexpr int      kHistoryRequestFirst = 4;
        constexpr int      kHistoryRequestLast  = 7;
        constexpr uint16_t kHistorySample       = 0xACu;
        constexpr unsigned kMinorHistoryTakes   = 4u;
        constexpr uint8_t  kMinorHistoryTag     = 0u;
        constexpr unsigned kHistoryTiers        = 4u;
        constexpr uint16_t kBroadcastSample     = 0x146u;
        constexpr unsigned kHiddenBroadcastTake = 11u;
        constexpr uint8_t  kMultipleSuspectsTag = 1u;
        constexpr unsigned kMaxHiddenTakes      = 4u;

        constexpr uint32_t kRetry               = 1u * kTicksPerSecond;
        constexpr uint32_t kLineTimeout         = 25u * kTicksPerSecond;
        constexpr uint32_t kRadioHoldTimeout    = 20u * kTicksPerSecond;
        constexpr uint32_t kRuleRelaxTimeout    = 25u * kTicksPerSecond;
        constexpr uint32_t kBailoutSettle       = kTicksPerSecond / 4u;
        constexpr float    kFuelLowSeconds      = 8.0f;
        constexpr uint32_t kTunnelAlertDelay    = 1u * kTicksPerSecond;
        constexpr uint32_t kRegainVisualRelax   = 10u * kTicksPerSecond;
        constexpr uint32_t kSpotWindow          = 15u * kTicksPerSecond;
        constexpr uint32_t kSpotSearchWindow    = 5u * kTicksPerSecond;
        constexpr uint32_t kSpotQuiet           = 10u * kTicksPerSecond;
        constexpr uint32_t kCheckInGiveUp       = 45u * kTicksPerSecond;
        constexpr int      kAirSupportTries     = 3;
        constexpr uint32_t kAirSupportGiveUp    = 15u * kTicksPerSecond;
        constexpr uint32_t kRoadblockWait       = 120u * kTicksPerSecond;
        constexpr uint32_t kRequestWindow       = 10u * kTicksPerSecond;
        constexpr uint32_t kRoadblockGiveUp     = 30u * kTicksPerSecond;
        constexpr int      kRoadblockTries      = 3;
        constexpr uint32_t kHiddenTakeTimeout   = 30u * kTicksPerSecond;
        constexpr int      kHistoryMinHeat      = 5;
        constexpr uint32_t kHistoryWindow       = 60u * kTicksPerSecond;
        constexpr int      kHeliMinHeat         = 4;
        constexpr unsigned kBackupReplyChance   = 4u;
        constexpr uint32_t kBackupWindow        = 15u * kTicksPerSecond;
        constexpr int      kDrivingLineMinHeat  = 3;
        constexpr float    kDrivingLineMinMph   = 45.0f;
        constexpr uint32_t kDrivingLineGap      = 60u * kTicksPerSecond;
        constexpr uint32_t kUpdateReplyWindow   = 30u * kTicksPerSecond;
        constexpr float    kThemeSettleSeconds  = 30.0f;
        constexpr uint32_t kVehicleReportWindow = 45u * kTicksPerSecond;
        constexpr float    kRainReportLevel     = 0.25f;

        constexpr uint8_t kCall = 0xE8;
        constexpr uint8_t kJump = 0xE9;
        constexpr uint8_t kPush = 0x68;
        constexpr uint8_t kNop  = 0x90;

        using QueueEventCall     = void* (__cdecl*)(unsigned size, const void* params, uintptr_t entry, uintptr_t binding, void* speaker);
        using RadioCall          = void* (__fastcall*)(void* speaker, void* unused);
        using RadioTypeCall      = void* (__fastcall*)(void* speaker, void* unused, uint32_t type);
        using StrategyResetCall  = void* (__fastcall*)(void* speaker, void* unused, char changed);
        using HazardAlertCall    = void* (__fastcall*)(void* speaker, void* unused, uint32_t hazard);
        using BackupReplyCall    = void* (__fastcall*)(void* dispatcher, void* unused, void* unit, int approved, uint32_t type);
        using LostSuspectCall    = int (__cdecl*)(int voice);
        using ClosestVoiceCall   = void* (__fastcall*)(uint32_t speech, void* unused, int needClose, int includeHeli);
        using RadioBusyCall      = bool (__cdecl*)();
        using FindPlayCountCall  = void* (__fastcall*)(uintptr_t counts, void* unused, uint32_t key);
        using FindCollectionCall = void* (__cdecl*)(uint32_t classKey, uint32_t collectionKey);
        using FindAttributeCall  = void* (__fastcall*)(const void* instance, void* unused, uint32_t key, uint32_t index);
        using SupportRequestCall = void (__fastcall*)(void* flow, void* unused, const void* message);
        using TakeOffsetCall     = int (__cdecl*)(const void* header, int take, uint32_t* offset, uint32_t* size);
        using BailoutTakesCall   = uint32_t (__fastcall*)(void* speaker, void* unused);
        using RadioVehicleCall   = uint32_t (__fastcall*)(void* speaker, void* unused);
        using FindVehicleCall    = void* (__cdecl*)(uint32_t handle);
        using FindInterfaceCall  = void* (__fastcall*)(void* parts, void* unused, uint32_t id);
        using FuelLeftCall       = float (__fastcall*)(void* gauge, void* unused);

        struct Patch {
            uintptr_t va;
            size_t    length;
            uint8_t   original[8];
        };

        struct RadioLine {
            uint32_t event;
            uint32_t queued;
            bool     heard;
        };

        enum class LineState { Waiting, Heard, Dropped };

        enum class RuleKind { LeadIns, Flag, MaxWait, Distance };

        struct Rule {
            uint32_t attributes;
            RuleKind kind;
            uint32_t field;
            uint8_t* value;
            uint32_t saved;
            bool     relaxed;
            uint32_t until;
        };

        enum class CheckInStep { Idle, Waiting, Speaking };
        enum class AirSupportStep { Ready, Request, Asking, Reply, Answering };
        enum class RoadblockStep { Ready, Calling, Update, Updating };
        enum class BackupStep { Idle, Called, Dispatch, Pilot };

        Patch     gPatches[16] = {};
        int       gPatchCount = 0;
        uintptr_t gQueueEventOriginal = kQueueEvent;
        uintptr_t gSupportRequestOriginal = kSupportRequest;
        uintptr_t gTakeCheckOriginal = kTakeCheck;
        bool      gTakesHooked = false;
        bool      gTakeChoiceHooked = false;

        bool gBailoutReasonOn = false;
        bool gCheckInOn = false;
        bool gAirSupportOn = false;
        bool gRoadblockOn = false;
        bool gHistoryOn = false;
        bool gBackupReplyOn = false;
        bool gSwarmingOn = false;
        bool gUpdateReplyOn = false;
        bool gThemeOn = false;
        bool gVehicleReportOn = false;
        bool gWeatherOn = false;

        bool     gOwnLine = false;
        void*    gOwnQueued = nullptr;
        bool     gRadioHeld = false;
        uint32_t gRadioHeldUntil = 0;
        uint32_t gLastPursuitTick = 0;

        Rule        gCallForBackupLeadIns  = { kCallForBackupAttributes, RuleKind::LeadIns, kLeadInList };
        Rule        gBackupReplyLeadIns    = { kBackupReplyAttributes, RuleKind::LeadIns, kLeadInList };
        Rule        gUnit911ReplyLeadIns   = { kUnit911ReplyAttributes, RuleKind::LeadIns, kLeadInList };
        Rule        gRegainVisualLeadIns   = { kRegainVisualAttributes, RuleKind::LeadIns, kLeadInList };
        Rule        gRegainVisualMaxWait   = { kRegainVisualAttributes, RuleKind::MaxWait };
        Rule        gRoadblockLeadIns      = { kCallForRoadblockAttributes, RuleKind::LeadIns, kLeadInList };
        Rule        gRoadblockNeedsClose   = { kCallForRoadblockAttributes, RuleKind::Flag, kNeedsCloseSpeaker };
        Rule        gRoadblockNeedsVehicle = { kCallForRoadblockAttributes, RuleKind::Flag, kNeedsSpeakerVehicle };
        Rule        gRoadblockMaxWait      = { kCallForRoadblockAttributes, RuleKind::MaxWait };
        Rule        gBailoutMaxWait        = { kHeliBailoutAttributes, RuleKind::MaxWait };
        Rule        gBailoutDistance       = { kHeliBailoutAttributes, RuleKind::Distance };
        Rule* const gRules[] = { &gCallForBackupLeadIns, &gBackupReplyLeadIns, &gUnit911ReplyLeadIns, &gRegainVisualLeadIns,
                                 &gRegainVisualMaxWait, &gRoadblockLeadIns, &gRoadblockNeedsClose, &gRoadblockNeedsVehicle,
                                 &gRoadblockMaxWait, &gBailoutMaxWait, &gBailoutDistance };

        bool     gHeliOut = false;
        uint32_t gHeliSeen = 0;
        bool     gHeliAlive = false;
        bool     gHeliDown = false;
        int      gHelisThisPursuit = 0;
        uint32_t gHeliJoinedAt = 0;

        uint32_t gIntentToRamTakes = kHighIntensityTakes;
        uint32_t gLostSuspectTakes = kHighIntensityTakes;
        bool     gStrategyResetChanged = true;

        uintptr_t gHitReturn = 0;
        bool      gBailoutPending = false;
        uint32_t  gBailoutAt = 0;
        void*     gBailoutSpeaker = nullptr;
        uint32_t  gBailoutTakes = 0;
        float     gBailoutFuel = 0.0f;
        bool      gBailoutFuelKnown = false;
        RadioLine gGoingDownLine = {};
        bool      gTunnelAlertPending = false;
        uint32_t  gTunnelAlertAt = 0;

        bool      gSpotUnusedNext = true;
        bool      gSpotPending = false;
        bool      gSpotInterrupt = false;
        uint32_t  gSpotAt = 0;
        uintptr_t gSpotVanilla = 0;
        bool      gSpotSaid = false;
        uint32_t  gSpotSaidAt = 0;
        RadioLine gSpotLine = {};

        CheckInStep gCheckInStep = CheckInStep::Idle;
        uint32_t    gCheckInStarted = 0;
        uint32_t    gCheckInNext = 0;
        RadioLine   gCheckInLine = {};

        AirSupportStep gAirSupportStep = AirSupportStep::Ready;
        bool           gAirSupportWanted = false;
        bool           gAirSupportAsked = false;
        int            gAirSupportFailures = 0;
        uint32_t       gAirSupportCaller = 0;
        uint32_t       gAirSupportStarted = 0;
        uint32_t       gAirSupportNext = 0;
        int            gAirSupportTries = 0;
        RadioLine      gAirSupportLine = {};

        bool          gRoadblockRequested = false;
        uint32_t      gRoadblockRequestedAt = 0;
        bool          gRoadblockAsked = false;
        RoadblockStep gRoadblockStep = RoadblockStep::Ready;
        uint32_t      gRoadblockStarted = 0;
        uint32_t      gRoadblockNext = 0;
        int           gRoadblockTries = 0;
        unsigned      gRoadblockUpdate = 0;
        RadioLine     gRoadblockLine = {};

        uint8_t* gHiddenTags[kMaxHiddenTakes] = {};
        uint8_t  gHiddenValues[kMaxHiddenTakes] = {};
        unsigned gHiddenCount = 0;
        uint32_t gHiddenAt = 0;

        bool      gHistoryAsked = false;
        bool      gHistoryHeard = false;
        uint32_t  gHistoryAskedAt = 0;
        bool      gHistoryTracked = false;
        RadioLine gHistoryLine = {};

        BackupStep gBackupStep = BackupStep::Idle;
        uint32_t   gBackupHeardAt = 0;
        bool       gBackupAnswered = false;
        bool       gBackupReplying = false;
        RadioLine  gBackupReplyLine = {};

        bool      gHeliAsked = false;
        RadioLine gHeliQuestion = {};
        bool      gUpdateReplyTracked = false;
        RadioLine gUpdateReplyLine = {};
        bool      gDrivingLineSaid = false;
        uint32_t  gDrivingLineAt = 0;

        bool      gThemeKnown = false;
        int       gPursuitTheme = 0;
        bool      gBroadcastTracked = false;
        RadioLine gBroadcastLine = {};
        bool      gVehicleReportDue = false;
        uint32_t  gVehicleReportAt = 0;
        RadioLine gVehicleLine = {};

        bool      gRainReported = false;
        RadioLine gWeatherLine = {};

#if defined(_DEBUG)
        FILE* gLog = nullptr;

        void OpenLog(HMODULE module) {
            char path[MAX_PATH] = "";
            const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
            char* dot = length && length < MAX_PATH ? std::strrchr(path, '.') : nullptr;
            if (!dot || dot < std::strrchr(path, '\\')) return;
            std::strcpy(dot, ".log");
            gLog = std::fopen(path, "w");
        }

        void WriteLog(const char* format, ...) {
            if (!gLog) return;
            char line[512] = "";
            va_list args;
            va_start(args, format);
            std::vsnprintf(line, sizeof(line) - 2, format, args);
            va_end(args);
            std::strcat(line, "\n");
            std::fputs(line, gLog);
            std::fflush(gLog);
        }

        void CloseLog() {
            if (gLog) std::fclose(gLog);
            gLog = nullptr;
        }

#define LOG(...) WriteLog(__VA_ARGS__)
#define STEP_NAME(step) ((step) == AirSupportStep::Asking || (step) == AirSupportStep::Request ? "unit's request" : "dispatch reply")
#else
        void OpenLog(HMODULE) {}
        void CloseLog() {}
        void WriteLog(const char*, ...);

#define LOG(...) ((void)sizeof((WriteLog(__VA_ARGS__), 0)))
#define STEP_NAME(step) ""
#endif

        int SafeCopy(void* destination, const void* source, size_t length) {
            __try {
                std::memcpy(destination, source, length);
                return 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

        bool Read(uintptr_t va, void* out, size_t length) {
            return SafeCopy(out, reinterpret_cast<const void*>(va), length) != 0;
        }

        bool WriteCode(uintptr_t va, const void* bytes, size_t length) {
            void* target = reinterpret_cast<void*>(va);
            DWORD oldProtect = 0;
            if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
            std::memcpy(target, bytes, length);
            FlushInstructionCache(GetCurrentProcess(), target, length);
            DWORD unused = 0;
            VirtualProtect(target, length, oldProtect, &unused);
            return true;
        }

        template <typename Function>
        uint32_t Address(Function function) {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function));
        }

        void* Pointer(uint32_t address) {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(address));
        }

        uint32_t CallOffset(uintptr_t call, uint32_t target) {
            return target - static_cast<uint32_t>(call + 5);
        }

        bool CallsTo(uintptr_t call, uint32_t target) {
            uint8_t code[5] = {};
            uint32_t offset = 0;
            if (!Read(call, code, sizeof(code)) || code[0] != kCall) return false;
            std::memcpy(&offset, code + 1, sizeof(offset));
            return offset == CallOffset(call, target);
        }

        bool Now(uint32_t& now) {
            return Read(kGameClock, &now, sizeof(now));
        }

        uint32_t SpeechModule() {
            uint32_t speech = 0;
            return Read(kSpeechModule, &speech, sizeof(speech)) ? speech : 0;
        }

        uint32_t Dispatcher() {
            uint32_t dispatcher = 0;
            const uint32_t speech = SpeechModule();
            return speech && Read(speech + kDispatcher, &dispatcher, sizeof(dispatcher)) ? dispatcher : 0;
        }

        int HeatLevel() {
            int heat = 0;
            const uint32_t speech = SpeechModule();
            return speech && Read(speech + kHeatLevel, &heat, sizeof(heat)) ? heat : 0;
        }

        float SuspectSpeed() {
            float speed = 0.0f;
            const uint32_t speech = SpeechModule();
            return speech && Read(speech + kSuspectSpeed, &speed, sizeof(speed)) ? speed : 0.0f;
        }

        float PursuitTime() {
            float pursuit = -1.0f;
            const uint32_t speech = SpeechModule();
            return speech && Read(speech + kPursuitTime, &pursuit, sizeof(pursuit)) ? pursuit : -1.0f;
        }

        bool InCooldown() {
            uint32_t state = 0;
            const uint32_t speech = SpeechModule();
            return speech && Read(speech + kPursuitState, &state, sizeof(state)) && state == kPursuitCooldown;
        }

        float RainLevel() {
            uint32_t rain = 0;
            float level = 0.0f;
            return Read(kRain, &rain, sizeof(rain)) && rain && Read(rain + kRainLevel, &level, sizeof(level)) ? level : 0.0f;
        }

        void* HeliRadio() {
            uint32_t heli = 0;
            uint32_t radio = 0;
            uint32_t table = 0;
            const uint32_t speech = SpeechModule();
            if (!Read(kHeliVehicle, &heli, sizeof(heli)) || !heli || !speech || !Read(speech + kHeliRadio, &radio, sizeof(radio))
                || !radio || !Read(radio, &table, sizeof(table)) || table != kHeliRadioVtable)
                return nullptr;
            return Pointer(radio);
        }

        uint32_t SpeakerVoice(void* speaker) {
            return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(speaker) + kRadioVoice);
        }

        bool HasVisual(void* speaker) {
            uint8_t visual = 0;
            return Read(reinterpret_cast<uintptr_t>(speaker) + kRadioHasVisual, &visual, sizeof(visual)) && visual;
        }

        bool Destroyed(void* speaker) {
            uint8_t destroyed = 0;
            return Read(Address(speaker) + kRadioDestroyed, &destroyed, sizeof(destroyed)) && destroyed;
        }

        bool Damaged(void* speaker) {
            float health = kFullHealth;
            return Read(Address(speaker) + kRadioHealth, &health, sizeof(health)) && health < kFullHealth;
        }

        template <typename Visit>
        void ForEachSpeaker(Visit visit) {
            uint32_t begin = 0;
            uint32_t end = 0;
            const uint32_t speech = SpeechModule();
            if (!speech || !Read(speech + kSpeakersBegin, &begin, sizeof(begin)) || !Read(speech + kSpeakersEnd, &end, sizeof(end))
                || end < begin || end - begin > 64u * kSpeakerSize)
                return;
            for (uint32_t entry = begin; entry < end; entry += kSpeakerSize) {
                uint32_t speaker = 0;
                if (Read(entry + kSpeakerVoice, &speaker, sizeof(speaker)) && speaker) visit(speaker);
            }
        }

        bool InChase(uint32_t unit) {
            bool found = false;
            ForEachSpeaker([&](uint32_t speaker) { found = found || speaker == unit; });
            return unit && found;
        }

        bool HasAirSupportVoice(uint32_t speaker) {
            uint32_t voice = 0;
            if (!Read(speaker + kRadioVoice, &voice, sizeof(voice))) return false;
            for (uint32_t allowed : kAirSupportVoices)
                if (voice == allowed) return true;
            return false;
        }

        bool SeesSuspect(uint32_t speaker) {
            uint8_t visual = 0;
            uint8_t close = 0;
            return Read(speaker + kRadioHasVisual, &visual, sizeof(visual)) && visual
                && Read(speaker + kRadioIsClose, &close, sizeof(close)) && close;
        }

        uint32_t PickAirSupportCaller() {
            uint32_t callers[32] = {};
            unsigned count = 0;
            ForEachSpeaker([&](uint32_t speaker) {
                if (count < sizeof(callers) / sizeof(callers[0]) && HasAirSupportVoice(speaker) && SeesSuspect(speaker))
                    callers[count++] = speaker;
            });
            return count ? callers[static_cast<unsigned>(std::rand()) % count] : 0;
        }

        uint32_t NextTakes(uint32_t& takes) {
            takes = takes == kNormalTakes ? kHighIntensityTakes : kNormalTakes;
            return takes;
        }

        template <size_t Count>
        void* QueueEvent(const Event& event, const uint32_t (&params)[Count], void* speaker) {
            return reinterpret_cast<QueueEventCall>(kQueueEvent)(sizeof(params), params, event.entry, event.binding, speaker);
        }

        void* CallRadio(void* speaker, unsigned slot) {
            void** table = *static_cast<void***>(speaker);
            return reinterpret_cast<RadioCall>(table[slot / sizeof(void*)])(speaker, nullptr);
        }

        void* CallRadio(void* speaker, unsigned slot, uint32_t type) {
            void** table = *static_cast<void***>(speaker);
            return reinterpret_cast<RadioTypeCall>(table[slot / sizeof(void*)])(speaker, nullptr, type);
        }

        void ResetPlayCount(const Event& event) {
            char* entry = static_cast<char*>(reinterpret_cast<FindPlayCountCall>(kFindPlayCount)(kPlayCounts, nullptr, event.key));
            if (entry) *reinterpret_cast<uint16_t*>(entry + kPlayCount) = 0;
        }

        bool RadioBusy() {
            return reinterpret_cast<RadioBusyCall>(kRadioBusy)();
        }

        uint32_t PlayingEvent() {
            uint32_t player = 0;
            uint32_t event = 0;
            return Read(kSpeechPlayer, &player, sizeof(player)) && player && Read(player + kPlayingEvent, &event, sizeof(event))
                ? event : 0;
        }

        uint32_t PlayingEntry() {
            uint32_t entry = 0;
            const uint32_t playing = PlayingEvent();
            return playing && Read(playing, &entry, sizeof(entry)) ? entry : 0;
        }

        bool Queued(uint32_t event) {
            for (unsigned i = 0; i < kQueueCount; ++i) {
                uint32_t begin = 0;
                uint32_t end = 0;
                const uintptr_t queue = kSpeechQueues + i * kQueueStride;
                if (!Read(queue, &begin, sizeof(begin)) || !Read(queue + kQueueEnd, &end, sizeof(end)) || end < begin
                    || end - begin > 256u * sizeof(uint32_t))
                    continue;
                for (uint32_t item = begin; item < end; item += sizeof(uint32_t)) {
                    uint32_t queued = 0;
                    if (Read(item, &queued, sizeof(queued)) && queued == event) return true;
                }
            }
            return false;
        }

        bool Listen(RadioLine& line, void* event, uint32_t now) {
            if (!event) return false;
            uint8_t* priority = static_cast<uint8_t*>(event) + kEventPriority;
            if (*priority < kLinePriority) *priority = kLinePriority;
            line = { Address(event), now, false };
            return true;
        }

        LineState Track(RadioLine& line, uint32_t now) {
            if (PlayingEvent() == line.event) {
                line.heard = true;
                return LineState::Waiting;
            }
            if (line.heard) return LineState::Heard;
            return Queued(line.event) && now - line.queued < kLineTimeout ? LineState::Waiting : LineState::Dropped;
        }

        template <typename Speak>
        void* OwnLine(Speak speak) {
            gOwnLine = true;
            void* event = speak();
            gOwnLine = false;
            return event;
        }

        template <typename Speak>
        void* OwnQueued(Speak speak) {
            gOwnQueued = nullptr;
            OwnLine(speak);
            return gOwnQueued;
        }

        void HoldRadio(uint32_t now) {
            gRadioHeld = true;
            gRadioHeldUntil = now + kRadioHoldTimeout;
        }

        void ReleaseRadio() {
            gRadioHeld = false;
        }

        bool RadioHeld(uint32_t now) {
            return gRadioHeld && now < gRadioHeldUntil && gRadioHeldUntil - now <= kRadioHoldTimeout;
        }

        uint8_t* FindRule(const Rule& rule) {
            void* collection = reinterpret_cast<FindCollectionCall>(kFindCollection)(kSpeechEventClass, rule.attributes);
            if (!collection) return nullptr;
            if (rule.kind == RuleKind::MaxWait || rule.kind == RuleKind::Distance) {
                const bool wait = rule.kind == RuleKind::MaxWait;
                const unsigned field = wait ? kLayoutMaxWait : kLayoutDistance;
                const float longest = wait ? kLongestMaxWait : kLongestDistance;
                uint32_t layout = 0;
                float value = 0.0f;
                if (!Read(reinterpret_cast<uintptr_t>(collection) + kCollectionLayout, &layout, sizeof(layout)) || !layout
                    || !Read(layout + field, &value, sizeof(value)) || !(value > 0.0f) || value > longest)
                    return nullptr;
                return static_cast<uint8_t*>(Pointer(layout + field));
            }

            const uint32_t instance[5] = { 0, Address(collection), 0, 0, 0 };
            uint8_t* first = static_cast<uint8_t*>(reinterpret_cast<FindAttributeCall>(kFindAttribute)(instance, nullptr, rule.field, 0));
            if (!first) return nullptr;
            if (rule.kind == RuleKind::Flag) {
                uint32_t value = 0;
                return Read(reinterpret_cast<uintptr_t>(first), &value, sizeof(value)) && value == 1u ? first : nullptr;
            }

            uint8_t* list = first - kListHeader;
            const uintptr_t at = reinterpret_cast<uintptr_t>(list);
            uint16_t count = 0;
            uint16_t size = 0;
            uint16_t flags = 0;
            uint32_t eventClass = 0;
            if (!Read(at + kListCount, &count, sizeof(count)) || !Read(at + kListItemSize, &size, sizeof(size))
                || !Read(at + kListFlags, &flags, sizeof(flags)) || !Read(reinterpret_cast<uintptr_t>(first), &eventClass, sizeof(eventClass))
                || !count || count > kMaxLeadIns || size != kLeadInSize || (flags & kListWideHeader) || eventClass != kSpeechEventClass)
                return nullptr;
            return list + kListCount;
        }

        void RelaxRule(Rule& rule, uint32_t now, uint32_t duration = kRuleRelaxTimeout) {
            if (rule.relaxed) {
                rule.until = now + duration;
                return;
            }
            if (!rule.value) rule.value = FindRule(rule);
            if (!rule.value) return;
            switch (rule.kind) {
            case RuleKind::LeadIns: {
                uint16_t* count = reinterpret_cast<uint16_t*>(rule.value);
                rule.saved = *count;
                *count = 0;
                break;
            }
            case RuleKind::Flag: {
                uint32_t* flag = reinterpret_cast<uint32_t*>(rule.value);
                rule.saved = *flag;
                *flag = 0;
                break;
            }
            case RuleKind::MaxWait:
            case RuleKind::Distance: {
                float* value = reinterpret_cast<float*>(rule.value);
                std::memcpy(&rule.saved, value, sizeof(rule.saved));
                *value = rule.kind == RuleKind::MaxWait ? kRelaxedMaxWait : kRelaxedDistance;
                break;
            }
            }
            rule.relaxed = true;
            rule.until = now + duration;
        }

        void RestoreRule(Rule& rule) {
            if (!rule.relaxed) return;
            if (rule.kind == RuleKind::LeadIns)
                *reinterpret_cast<uint16_t*>(rule.value) = static_cast<uint16_t>(rule.saved);
            else
                std::memcpy(rule.value, &rule.saved, sizeof(rule.saved));
            rule.relaxed = false;
        }

        void RestoreExpiredRules(bool clock, uint32_t now) {
            for (Rule* rule : gRules)
                if (rule->relaxed && (!clock || now >= rule->until || rule->until - now > kRuleRelaxTimeout))
                    RestoreRule(*rule);
        }

        void RestoreAllRules() {
            for (Rule* rule : gRules) RestoreRule(*rule);
        }

        uint8_t* FindTakeTag(uint16_t sample, uint16_t voice, unsigned take, uint8_t expected) {
            uint32_t table = 0;
            uint32_t count = 0;
            if (!Read(kSampleHeaders, &table, sizeof(table)) || !table || !Read(kSampleHeaderCount, &count, sizeof(count))
                || count > kMaxSampleHeaders)
                return nullptr;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t header = 0;
                uint16_t id[2] = {};
                uint8_t shape[2] = {};
                if (!Read(table + i * kSampleEntrySize + kSampleEntryHeader, &header, sizeof(header)) || !header
                    || !Read(header, id, sizeof(id)) || id[0] != sample || id[1] != voice
                    || !Read(header + kHeaderDimensions, shape, sizeof(shape)) || !(shape[0] & kDimensionMask) || take >= shape[1])
                    continue;
                const uint32_t tag = header + kHeaderTakeList + (2u + (shape[0] & kDimensionMask)) * take + 2u;
                uint8_t value = 0;
                return Read(tag, &value, sizeof(value)) && value == expected ? static_cast<uint8_t*>(Pointer(tag)) : nullptr;
            }
            return nullptr;
        }

        void ShowHiddenTakes() {
            while (gHiddenCount > 0) {
                --gHiddenCount;
                *gHiddenTags[gHiddenCount] = gHiddenValues[gHiddenCount];
            }
        }

        bool HideTakes(uint16_t sample, uint16_t voice, unsigned first, unsigned count, uint8_t expected, uint32_t now) {
            if (gHiddenCount || count > kMaxHiddenTakes) return false;
            for (unsigned take = first; take < first + count; ++take) {
                uint8_t* tag = FindTakeTag(sample, voice, take, expected);
                if (!tag) {
                    ShowHiddenTakes();
                    return false;
                }
                gHiddenTags[gHiddenCount] = tag;
                gHiddenValues[gHiddenCount] = *tag;
                ++gHiddenCount;
                *tag = kUnusedTag;
            }
            gHiddenAt = now;
            return true;
        }

        bool SampleOf(const void* header, uint16_t& sample, uint16_t& voice) {
            uint16_t id[2] = {};
            if (!Read(reinterpret_cast<uintptr_t>(header), id, sizeof(id))) return false;
            sample = id[0];
            voice = id[1];
            return true;
        }

        bool IsHeliCheckIn(const void* header) {
            uint16_t sample = 0;
            uint16_t voice = 0;
            return SampleOf(header, sample, voice) && sample == kCheckInSample && voice == static_cast<uint16_t>(kHeliVoice);
        }

#if defined(_DEBUG)
        struct SampleEntries {
            uint32_t              key;
            std::vector<uint32_t> entries;
        };

        struct TakeFile {
            uint32_t offset;
            uint16_t sample;
            uint16_t take;
        };

        std::vector<SampleEntries> gSampleEntries;

        uint32_t SampleKey(uint16_t sample, uint16_t voice) {
            return (static_cast<uint32_t>(sample) << 16) | voice;
        }

        std::vector<uint8_t> ReadSpeechIndex() {
            char path[MAX_PATH] = "";
            const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
            char* slash = length && length < MAX_PATH ? std::strrchr(path, '\\') : nullptr;
            std::vector<uint8_t> bytes;
            if (!slash) return bytes;
            slash[1] = 0;
            FILE* file = std::fopen((std::string(path) + "SOUND\\SPEECH\\copspeech.idx").c_str(), "rb");
            if (!file) return bytes;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            if (size > 0) {
                bytes.resize(static_cast<size_t>(size));
                if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
            }
            std::fclose(file);
            return bytes;
        }

        uint32_t Little32(const std::vector<uint8_t>& bytes, size_t at) {
            if (at + 4 > bytes.size()) return 0;
            return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
        }

        uint16_t Little16(const std::vector<uint8_t>& bytes, size_t at) {
            if (at + 2 > bytes.size()) return 0;
            return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        }

        void LoadSpeechEntries() {
            const std::vector<uint8_t> idx = ReadSpeechIndex();
            const uint32_t groups = Little32(idx, 4);
            std::vector<TakeFile> files;
            for (uint32_t group = 0; group < groups && 0x58u + 16u * (group + 1u) <= idx.size(); ++group) {
                const size_t record = 0x58u + 16u * group;
                const uint32_t meta = Little32(idx, record + 8);
                const uint32_t big = Little32(idx, record + 12);
                if (meta + 12u > idx.size()) continue;
                const unsigned width = 2u + idx[meta + 4];
                const unsigned takes = idx[meta + 5];
                const uint16_t sample = static_cast<uint16_t>(gSampleEntries.size());
                gSampleEntries.push_back({ SampleKey(Little16(idx, meta), Little16(idx, meta + 2)), std::vector<uint32_t>(takes, 0u) });
                for (unsigned take = 0; take < takes && meta + 12u + width * (take + 1u) <= idx.size(); ++take) {
                    const size_t at = meta + 12u + width * take;
                    const uint32_t page = (static_cast<uint32_t>(idx[at]) << 8) | idx[at + 1];
                    files.push_back({ big + page * 256u, sample, static_cast<uint16_t>(take) });
                }
            }
            std::sort(files.begin(), files.end(), [](const TakeFile& a, const TakeFile& b) { return a.offset < b.offset; });
            for (size_t i = 0; i < files.size(); ++i) gSampleEntries[files[i].sample].entries[files[i].take] = static_cast<uint32_t>(i);
            std::sort(gSampleEntries.begin(), gSampleEntries.end(), [](const SampleEntries& a, const SampleEntries& b) { return a.key < b.key; });
            LOG("Speech: %lu takes indexed from copspeech.idx.", static_cast<unsigned long>(files.size()));
        }

        void LogTake(const void* header, int take) {
            uint16_t sample = 0;
            uint16_t voice = 0;
            if (!SampleOf(header, sample, voice)) return;
            const uint32_t key = SampleKey(sample, voice);
            const auto found = std::lower_bound(gSampleEntries.begin(), gSampleEntries.end(), key,
                                                [](const SampleEntries& item, uint32_t value) { return item.key < value; });
            if (found == gSampleEntries.end() || found->key != key || take < 0 || static_cast<size_t>(take) >= found->entries.size()) {
                LOG("Speech: ENTRY_????? (0x%04X)", sample);
                return;
            }
            LOG("Speech: ENTRY_%05lu (0x%04X)", static_cast<unsigned long>(found->entries[static_cast<size_t>(take)]), sample);
        }
#else
        void LoadSpeechEntries() {}
        void LogTake(const void*, int) {}
#endif

        void NoteHistoryRequest(const void* header, int take) {
            uint32_t now = 0;
            if (!gHistoryOn || take < kHistoryRequestFirst || take > kHistoryRequestLast || !IsHeliCheckIn(header) || !Now(now))
                return;
            gHistoryAsked = true;
            gHistoryHeard = false;
            gHistoryAskedAt = now;
        }

        int __cdecl TakeLoadedHook(const void* header, int take, uint32_t* offset, uint32_t* size) {
            NoteHistoryRequest(header, take);
            LogTake(header, take);
            return reinterpret_cast<TakeOffsetCall>(kTakeOffset)(header, take, offset, size);
        }

        bool __cdecl AllowCheckInTake(const void* header, int take) {
            if (!gBackupReplyOn || !IsHeliCheckIn(header)) return true;
            const bool backupTake = take >= 0 && take < kBackupReplyTakes;
            return gBackupReplying ? backupTake : !backupTake;
        }

        __declspec(naked) void TakeCheckHook() {
            __asm {
                push eax
                push ecx
                push edx
                push ebx
                push esi
                call AllowCheckInTake
                add esp, 8
                test al, al
                pop edx
                pop ecx
                pop eax
                jz blocked
                jmp dword ptr [gTakeCheckOriginal]
            blocked:
                xor eax, eax
                ret
            }
        }

        void* __fastcall HeliIntentToRam(void* speaker, void*) {
            const uint32_t params[] = { SpeakerVoice(speaker), NextTakes(gIntentToRamTakes) };
            return QueueEvent(kIntentToRam, params, speaker);
        }

        void* __fastcall HeliStrategyReset(void* speaker, void*, char) {
            gStrategyResetChanged = !gStrategyResetChanged;
            return reinterpret_cast<StrategyResetCall>(kStrategyReset.function)(speaker, nullptr, gStrategyResetChanged ? 1 : 0);
        }

        void RelaxRoadblockRules(uint32_t now) {
            RelaxRule(gRoadblockLeadIns, now);
            RelaxRule(gRoadblockNeedsClose, now);
            RelaxRule(gRoadblockNeedsVehicle, now);
            RelaxRule(gRoadblockMaxWait, now);
        }

        void RestoreRoadblockRules() {
            RestoreRule(gRoadblockLeadIns);
            RestoreRule(gRoadblockNeedsClose);
            RestoreRule(gRoadblockNeedsVehicle);
            RestoreRule(gRoadblockMaxWait);
        }

        void FinishRoadblock() {
            gRoadblockStep = RoadblockStep::Ready;
            ReleaseRadio();
            RestoreRoadblockRules();
        }

        bool HeliMayCallRoadblock(uint32_t now) {
            return gRoadblockOn && !gRoadblockAsked && gHeliOut && now >= gHeliJoinedAt && now - gHeliJoinedAt >= kRoadblockWait;
        }

        bool RadioFreeForRoadblock() {
            return gAirSupportStep == AirSupportStep::Ready && gRoadblockStep == RoadblockStep::Ready && gCheckInStep == CheckInStep::Idle;
        }

        void __fastcall SupportRequestHook(void* flow, void*, const void* message) {
            reinterpret_cast<SupportRequestCall>(gSupportRequestOriginal)(flow, nullptr, message);
            uint32_t type = 0;
            uint32_t now = 0;
            if (!Read(reinterpret_cast<uintptr_t>(message) + kRequestType, &type, sizeof(type)) || !Now(now)) return;
            if (type != kRoadblockRequest && type != kStrategyRequest) return;
            LOG("Roadblock: the police are setting up a roadblock.");
            gRoadblockRequested = true;
            gRoadblockRequestedAt = now;
        }

        void* CallForRoadblock(void* heli, uint32_t now) {
            RelaxRoadblockRules(now);
            ResetPlayCount(kCallForRoadblock);
            void* event = OwnLine([heli] { return CallRadio(heli, kCallForRoadblockSlot); });
            if (!Listen(gRoadblockLine, event, now)) {
                RestoreRoadblockRules();
                return nullptr;
            }
            LOG("Roadblock: the pilot's call for a roadblock is queued.");
            gRoadblockAsked = true;
            gRoadblockStep = RoadblockStep::Calling;
            gRoadblockStarted = now;
            gRoadblockTries = 0;
            HoldRadio(now);
            return event;
        }

        void AnswerRoadblockRequest(void* heli, uint32_t now) {
            if (!gRoadblockRequested) return;
            if (now < gRoadblockRequestedAt || now - gRoadblockRequestedAt > kRequestWindow || !HeliMayCallRoadblock(now)) {
                LOG("Roadblock: the pilot can't take this request (too soon after joining, already asked, or out of time).");
                gRoadblockRequested = false;
                return;
            }
            if (!HasVisual(heli) || !RadioFreeForRoadblock()) return;
            gRoadblockRequested = false;
            if (!CallForRoadblock(heli, now)) LOG("Roadblock: the game turned the pilot's call down.");
        }

        void* UpdateRoadblock() {
            const uint32_t dispatcher = Dispatcher();
            if (!dispatcher) return nullptr;
            gRoadblockUpdate = (gRoadblockUpdate + 1u) % (sizeof(kRoadblockUpdates) / sizeof(kRoadblockUpdates[0]));
            const uint32_t params[] = { SpeakerVoice(Pointer(dispatcher)), Address(CallRadio(Pointer(dispatcher), kIntensitySlot)),
                                        kRoadblockUpdates[gRoadblockUpdate][0], kRoadblockUpdates[gRoadblockUpdate][1] };
            ResetPlayCount(kRoadblockUpdate);
            return OwnLine([&params, dispatcher] { return QueueEvent(kRoadblockUpdate, params, Pointer(dispatcher)); });
        }

        void AdvanceRoadblock(uint32_t now) {
            if (gRoadblockStep == RoadblockStep::Ready) return;
            if (now < gRoadblockStarted || now - gRoadblockStarted > kRoadblockGiveUp) {
                LOG("Roadblock: gave up on dispatch's answer.");
                FinishRoadblock();
                return;
            }

            switch (gRoadblockStep) {
            case RoadblockStep::Calling:
                switch (Track(gRoadblockLine, now)) {
                case LineState::Heard:
                    LOG("Roadblock: the pilot's call played.");
                    RestoreRoadblockRules();
                    gRoadblockStep = RoadblockStep::Update;
                    gRoadblockNext = now;
                    break;
                case LineState::Dropped:
                    LOG("Roadblock: the game dropped the pilot's call before it played; he can take the next request.");
                    FinishRoadblock();
                    gRoadblockAsked = false;
                    break;
                default:
                    break;
                }
                break;
            case RoadblockStep::Update:
                if (now < gRoadblockNext) return;
                if (!Listen(gRoadblockLine, UpdateRoadblock(), now)) {
                    LOG("Roadblock: the game turned dispatch's answer down, trying again in a second.");
                    gRoadblockNext = now + kRetry;
                    return;
                }
                LOG("Roadblock: dispatch's answer is queued.");
                gRoadblockStep = RoadblockStep::Updating;
                HoldRadio(now);
                break;
            case RoadblockStep::Updating:
                switch (Track(gRoadblockLine, now)) {
                case LineState::Heard:
                    LOG("Roadblock: dispatch's answer played.");
                    FinishRoadblock();
                    break;
                case LineState::Dropped:
                    LOG("Roadblock: the game dropped dispatch's answer before it played.");
                    if (++gRoadblockTries >= kRoadblockTries) {
                        FinishRoadblock();
                        break;
                    }
                    gRoadblockStep = RoadblockStep::Update;
                    gRoadblockNext = now + kRetry;
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }

        void StartCheckIn(uint32_t now) {
            if (!gCheckInOn) return;
            gCheckInStep = CheckInStep::Waiting;
            gCheckInStarted = now;
            gCheckInNext = now;
        }

        void CheckIn(void* speaker, uint32_t now) {
            if (gCheckInStep == CheckInStep::Idle) return;
            if (now < gCheckInStarted || now - gCheckInStarted > kCheckInGiveUp) {
                LOG("Check-in: gave up.");
                RestoreRule(gUnit911ReplyLeadIns);
                gCheckInStep = CheckInStep::Idle;
                return;
            }

            if (gCheckInStep == CheckInStep::Waiting) {
                if (now < gCheckInNext || RadioBusy() || gAirSupportStep != AirSupportStep::Ready || gRoadblockStep != RoadblockStep::Ready)
                    return;
                ResetPlayCount(kUnit911Reply);
                RelaxRule(gUnit911ReplyLeadIns, now);
                void* event = OwnLine([speaker] { return reinterpret_cast<RadioCall>(kUnit911Reply.function)(speaker, nullptr); });
                if (!Listen(gCheckInLine, event, now)) {
                    LOG("Check-in: the game turned the pilot's line down, trying again in a second.");
                    RestoreRule(gUnit911ReplyLeadIns);
                    gCheckInNext = now + kRetry;
                    return;
                }
                LOG("Check-in: the pilot's line is queued.");
                gCheckInStep = CheckInStep::Speaking;
                return;
            }

            switch (Track(gCheckInLine, now)) {
            case LineState::Heard:
                LOG("Check-in: the pilot's line played.");
                RestoreRule(gUnit911ReplyLeadIns);
                gCheckInStep = CheckInStep::Idle;
                break;
            case LineState::Dropped:
                LOG("Check-in: the game dropped the pilot's line before it played.");
                RestoreRule(gUnit911ReplyLeadIns);
                gCheckInStep = CheckInStep::Waiting;
                gCheckInNext = now + kRetry;
                break;
            default:
                break;
            }
        }

        void FinishAirSupport(uint32_t now, bool checkIn) {
            gAirSupportStep = AirSupportStep::Ready;
            ReleaseRadio();
            RestoreRule(gCallForBackupLeadIns);
            RestoreRule(gBackupReplyLeadIns);
            if (!gAirSupportAsked && gAirSupportWanted && ++gAirSupportFailures >= kAirSupportTries) {
                LOG("Air support: nobody managed to ask for another helicopter, so the units stop trying.");
                gAirSupportWanted = false;
            }
            if (checkIn && HeliRadio()) StartCheckIn(now);
        }

        bool StartAirSupport(uint32_t now) {
            if (!gAirSupportOn || gAirSupportStep != AirSupportStep::Ready || gRoadblockStep != RoadblockStep::Ready || PursuitTime() < 0.0f)
                return false;
            LOG("Air support: the last helicopter is gone, so a unit that can see you asks for another one.");
            gAirSupportStep = AirSupportStep::Request;
            gAirSupportStarted = now;
            gAirSupportNext = now;
            gAirSupportTries = 0;
            return true;
        }

        void* AskForAirSupport() {
            ResetPlayCount(kCallForBackup);
            return OwnLine([] { return CallRadio(Pointer(gAirSupportCaller), kCallForBackupSlot, kAirSupport); });
        }

        void* AnswerAirSupport() {
            const uint32_t dispatcher = Dispatcher();
            if (!dispatcher) return nullptr;
            ResetPlayCount(kDispBackupReply);
            return OwnLine([dispatcher] {
                return reinterpret_cast<BackupReplyCall>(kDispBackupReply.function)(Pointer(dispatcher), nullptr, Pointer(gAirSupportCaller),
                                                                                    kApproved, kAirSupport);
            });
        }

        void SayAirSupportLine(AirSupportStep listening, Rule& rule, void* event, uint32_t now) {
            if (!Listen(gAirSupportLine, event, now)) {
                LOG("Air support: the game turned down the %s line, trying again in a second.", STEP_NAME(listening));
                RestoreRule(rule);
                gAirSupportNext = now + kRetry;
                return;
            }
            LOG("Air support: the %s line is queued.", STEP_NAME(listening));
            gAirSupportStep = listening;
            HoldRadio(now);
        }

        void FollowAirSupportLine(AirSupportStep listening, Rule& rule, AirSupportStep retry, uint32_t now) {
            switch (Track(gAirSupportLine, now)) {
            case LineState::Heard:
                LOG("Air support: the %s line played.", STEP_NAME(listening));
                RestoreRule(rule);
                gAirSupportTries = 0;
                if (listening == AirSupportStep::Answering) {
                    gAirSupportWanted = false;
                    FinishAirSupport(now, true);
                    return;
                }
                gAirSupportAsked = true;
                gAirSupportWanted = false;
                gAirSupportStep = AirSupportStep::Reply;
                gAirSupportStarted = now;
                gAirSupportNext = now;
                break;
            case LineState::Dropped:
                LOG("Air support: the game dropped the %s line before it played.", STEP_NAME(listening));
                RestoreRule(rule);
                if (++gAirSupportTries >= kAirSupportTries) {
                    FinishAirSupport(now, true);
                    return;
                }
                gAirSupportStep = retry;
                gAirSupportNext = now + kRetry;
                break;
            default:
                break;
            }
        }

        void AdvanceAirSupport(uint32_t now) {
            if (gAirSupportStep == AirSupportStep::Ready) return;
            const bool waiting = gAirSupportStep == AirSupportStep::Request || gAirSupportStep == AirSupportStep::Reply;
            if (waiting && (now < gAirSupportStarted || now - gAirSupportStarted > kAirSupportGiveUp)) {
                LOG("Air support: the %s line couldn't be queued in time.", STEP_NAME(gAirSupportStep));
                FinishAirSupport(now, true);
                return;
            }

            switch (gAirSupportStep) {
            case AirSupportStep::Request:
                if (HeliRadio()) {
                    LOG("Air support: a helicopter is already up, so nobody asks.");
                    FinishAirSupport(now, false);
                    return;
                }
                if (now < gAirSupportNext || RadioBusy()) return;
                gAirSupportCaller = PickAirSupportCaller();
                if (!gAirSupportCaller) {
                    gAirSupportNext = now + kRetry;
                    return;
                }
                LOG("Air support: unit with voice %lu can see you and asks for it.",
                    static_cast<unsigned long>(SpeakerVoice(Pointer(gAirSupportCaller))));
                RelaxRule(gCallForBackupLeadIns, now);
                SayAirSupportLine(AirSupportStep::Asking, gCallForBackupLeadIns, AskForAirSupport(), now);
                break;
            case AirSupportStep::Asking:
                FollowAirSupportLine(AirSupportStep::Asking, gCallForBackupLeadIns, AirSupportStep::Request, now);
                break;
            case AirSupportStep::Reply:
                if (now < gAirSupportNext) return;
                if (!InChase(gAirSupportCaller)) {
                    LOG("Air support: the unit left the chase before dispatch could answer.");
                    FinishAirSupport(now, true);
                    return;
                }
                RelaxRule(gBackupReplyLeadIns, now);
                SayAirSupportLine(AirSupportStep::Answering, gBackupReplyLeadIns, AnswerAirSupport(), now);
                break;
            case AirSupportStep::Answering:
                FollowAirSupportLine(AirSupportStep::Answering, gBackupReplyLeadIns, AirSupportStep::Reply, now);
                break;
            default:
                break;
            }
        }

        bool FuelLeft(void* speaker, float& seconds) {
            uint32_t table = 0;
            uint32_t function = 0;
            if (!gBailoutReasonOn || !Read(Address(speaker), &table, sizeof(table)) || !Read(table + kRadioVehicleSlot, &function, sizeof(function)))
                return false;
            const uint32_t handle = reinterpret_cast<RadioVehicleCall>(function)(speaker, nullptr);
            void* vehicle = handle ? reinterpret_cast<FindVehicleCall>(kFindVehicle)(handle) : nullptr;
            uint32_t parts = 0;
            if (!vehicle || !Read(Address(vehicle) + kVehicleParts, &parts, sizeof(parts)) || !parts) return false;
            void* gauge = reinterpret_cast<FindInterfaceCall>(kFindInterface)(Pointer(parts), nullptr, kFuelGauge);
            uint32_t gaugeTable = 0;
            uint32_t fuelLeft = 0;
            if (!gauge || !Read(Address(gauge), &gaugeTable, sizeof(gaugeTable)) || !Read(gaugeTable + kFuelLeftSlot, &fuelLeft, sizeof(fuelLeft)))
                return false;
            seconds = reinterpret_cast<FuelLeftCall>(fuelLeft)(gauge, nullptr);
            return true;
        }

        void* ReportDamage(void* speaker, uint32_t now) {
            if (gRoadblockStep != RoadblockStep::Ready) FinishRoadblock();
            RelaxRule(gBailoutMaxWait, now);
            RelaxRule(gBailoutDistance, now);
            ResetPlayCount(kHeliBailout);
            const uint32_t params[] = { SpeakerVoice(speaker), kDamagedTakes };
            void* event = OwnLine([&params, speaker] { return QueueEvent(kHeliBailout, params, speaker); });
            if (!Listen(gGoingDownLine, event, now)) {
                LOG("Going down: the game turned the pilot's damage report down.");
                return nullptr;
            }
            LOG("Going down: the pilot's damage report is queued.");
            return event;
        }

        void* GamesOwnBailout(void* speaker, uint32_t now) {
            RelaxRule(gBailoutMaxWait, now);
            RelaxRule(gBailoutDistance, now);
            ResetPlayCount(kHeliBailout);
            void* event = OwnLine([speaker] { return reinterpret_cast<RadioCall>(kHeliBailout.function)(speaker, nullptr); });
            Listen(gGoingDownLine, event, now);
            return event;
        }

        void* SearchPattern(uint32_t now) {
            ResetPlayCount(kLostSuspect);
            const uint32_t params[] = { kHeliVoice, NextTakes(gLostSuspectTakes) };
            void* event = OwnLine([&params] { return QueueEvent(kLostSuspect, params, nullptr); });
            Listen(gGoingDownLine, event, now);
            LOG("Losing you: the pilot's search pattern line was %s.", event ? "queued" : "turned down by the game");
            return event;
        }

        void* __fastcall HeliBailout(void* speaker, void*) {
            const bool hit = gHitReturn && reinterpret_cast<uintptr_t>(_ReturnAddress()) == gHitReturn;
            uint32_t now = 0;
            if (!Now(now)) return reinterpret_cast<RadioCall>(kHeliBailout.function)(speaker, nullptr);
            if (gHeliDown) {
                LOG("Going down: skipped the game's second going-down call.");
                return nullptr;
            }
            if (gTunnelAlertPending) LOG("Tunnel warning: dropped, the helicopter is going down.");
            gTunnelAlertPending = false;
            gHeliDown = true;
            if (hit) {
                LOG("Going down: the helicopter was hit hard, so the pilot reports damage.");
                return ReportDamage(speaker, now);
            }
            if (!gBailoutReasonOn) return GamesOwnBailout(speaker, now);

            gBailoutPending = true;
            gBailoutAt = now;
            gBailoutSpeaker = speaker;
            gBailoutFuelKnown = FuelLeft(speaker, gBailoutFuel);
            gBailoutTakes = reinterpret_cast<BailoutTakesCall>(kBailoutTakes)(speaker, nullptr);
            return nullptr;
        }

        void ResolveBailout(uint32_t now) {
            if (!gBailoutPending || (now >= gBailoutAt && now - gBailoutAt < kBailoutSettle)) return;
            gBailoutPending = false;
            void* speaker = gBailoutSpeaker;
            if (Destroyed(speaker) || Damaged(speaker)) {
                LOG("Going down: the helicopter was shot down, so the pilot reports damage.");
                ReportDamage(speaker, now);
                return;
            }
            if (gBailoutTakes != kFuelTakes || !gBailoutFuelKnown || gBailoutFuel <= kFuelLowSeconds) {
                LOG("Going down: the game's own reason (takes %lu, %d s of fuel left).", static_cast<unsigned long>(gBailoutTakes),
                    gBailoutFuelKnown ? static_cast<int>(gBailoutFuel) : -1);
                GamesOwnBailout(speaker, now);
                return;
            }
            LOG("Losing you: the helicopter lost sight of you with %d s of fuel left and no damage.", static_cast<int>(gBailoutFuel));
            SearchPattern(now);
        }

        void* __fastcall HeliHazardAlert(void* speaker, void*, uint32_t hazard) {
            uint32_t now = 0;
            if (gHeliDown || Destroyed(speaker)) {
                LOG("Hazard warning: dropped, the helicopter is leaving the chase.");
                return nullptr;
            }
            if (hazard != kTunnelAhead || !Now(now))
                return reinterpret_cast<HazardAlertCall>(kHazardAlert.function)(speaker, nullptr, hazard);
            gTunnelAlertPending = true;
            gTunnelAlertAt = now + kTunnelAlertDelay;
            return nullptr;
        }

        void TunnelAlert(void* speaker, uint32_t now) {
            if (!gTunnelAlertPending || now < gTunnelAlertAt) return;
            gTunnelAlertPending = false;
            if (gHeliDown || Destroyed(speaker)) return;
            LOG("Tunnel warning: the pilot warns about the tunnel.");
            reinterpret_cast<HazardAlertCall>(kHazardAlert.function)(speaker, nullptr, kTunnelAhead);
        }

        int __cdecl HeliLostSuspect(int voice) {
            uint32_t heli = 0;
            if (!SpeechModule() || !Read(kHeliVehicle, &heli, sizeof(heli)) || !heli) {
                if (voice != static_cast<int>(kHeliVoice)) return reinterpret_cast<LostSuspectCall>(kLostSuspect.function)(voice);
                LOG("Losing you: the helicopter is gone, so a ground unit makes the call instead of its pilot.");
                return reinterpret_cast<LostSuspectCall>(kLostSuspect.function)(0);
            }

            ResetPlayCount(kLostSuspect);
            const uint32_t params[] = { kHeliVoice, NextTakes(gLostSuspectTakes) };
            void* event = QueueEvent(kLostSuspect, params, nullptr);
            LOG("Losing you: the pilot's line was %s.", event ? "queued" : "turned down by the game");
            return static_cast<int>(kHeliVoice);
        }

        void* SpotYou(void* speaker, uint32_t now, bool interrupt) {
            RelaxRule(gRegainVisualLeadIns, now, kRegainVisualRelax);
            RelaxRule(gRegainVisualMaxWait, now, kRegainVisualRelax);
            ResetPlayCount(kRegainVisual);
            const uint32_t params[] = { SpeakerVoice(speaker), kNormalTakes };
            void* event = interrupt ? OwnLine([&params, speaker] { return QueueEvent(kRegainVisual, params, speaker); })
                                    : QueueEvent(kRegainVisual, params, speaker);
            if (Listen(gSpotLine, event, now) && interrupt) static_cast<uint8_t*>(event)[kEventPriority] = kInterruptPriority;
            gSpotSaid = true;
            gSpotSaidAt = now;
            LOG("Spotting you: the pilot's line was %s.", !event ? "turned down by the game" : interrupt ? "queued to cut in" : "queued");
            return event;
        }

        void* HeliSpots(void* speaker, uintptr_t vanilla) {
            uint32_t now = 0;
            if (!Now(now)) return reinterpret_cast<RadioCall>(vanilla)(speaker, nullptr);
            if (gSpotSaid && now >= gSpotSaidAt && now - gSpotSaidAt < kSpotQuiet) {
                LOG("Spotting you: the pilot just said it, so the game's own call is skipped.");
                return nullptr;
            }
            if (gSpotPending && gSpotInterrupt) {
                LOG("Spotting you: the pilot's arrival line is still waiting, so the game's own call is skipped.");
                return nullptr;
            }
            const bool unused = gSpotUnusedNext;
            gSpotUnusedNext = !gSpotUnusedNext;
            if (InCooldown()) {
                LOG("Spotting you: the search isn't over yet, so the pilot waits until it really is.");
                gSpotPending = true;
                gSpotInterrupt = false;
                gSpotVanilla = unused ? 0u : vanilla;
                gSpotAt = now;
                return nullptr;
            }
            if (!unused) {
                LOG("Spotting you: this time the pilot uses the game's own takes.");
                gSpotSaid = true;
                gSpotSaidAt = now;
                return reinterpret_cast<RadioCall>(vanilla)(speaker, nullptr);
            }
            if (!RadioHeld(now)) return SpotYou(speaker, now, false);
            LOG("Spotting you: the radio is busy with an exchange, so the pilot waits for it.");
            gSpotPending = true;
            gSpotVanilla = 0;
            gSpotAt = now;
            return nullptr;
        }

        void* __fastcall HeliRegainsVisual(void* speaker, void*) {
            return HeliSpots(speaker, kRegainVisual.function);
        }

        void* __fastcall HeliSpotted(void* speaker, void*) {
            return HeliSpots(speaker, kSpotted.function);
        }

        void PendingSpot(void* speaker, uint32_t now) {
            if (!gSpotPending) return;
            const bool expired = now < gSpotAt || now - gSpotAt > (gSpotInterrupt ? kSpotWindow : kSpotSearchWindow);
            if (InCooldown()) {
                if (gSpotInterrupt) gSpotAt = now;
                else if (expired) {
                    LOG("Spotting you: the search went on, so the pilot never really spotted you.");
                    gSpotPending = false;
                }
                return;
            }
            if (!gSpotInterrupt && RadioHeld(now)) return;
            if (!expired) {
                if (!HasVisual(speaker)) return;
                if (gSpotVanilla) {
                    gSpotSaid = true;
                    gSpotSaidAt = now;
                    reinterpret_cast<RadioCall>(gSpotVanilla)(speaker, nullptr);
                }
                else SpotYou(speaker, now, gSpotInterrupt);
            }
            gSpotPending = false;
            gSpotInterrupt = false;
            gSpotVanilla = 0;
        }

        void DispatchHistory(uint32_t now) {
            const uint32_t dispatcher = Dispatcher();
            if (!dispatcher) return;
            unsigned tier = static_cast<unsigned>(std::rand()) % kHistoryTiers;
            if (tier == 0 && !HideTakes(kHistorySample, kDispatcherVoice, 0, kMinorHistoryTakes, kMinorHistoryTag, now))
                tier = 1u + static_cast<unsigned>(std::rand()) % (kHistoryTiers - 1u);
            ResetPlayCount(kDriverHistory);
            const uint32_t params[] = { SpeakerVoice(Pointer(dispatcher)), 1u << tier };
            void* event = OwnLine([&params, dispatcher] { return QueueEvent(kDriverHistory, params, Pointer(dispatcher)); });
            if (!Listen(gHistoryLine, event, now)) {
                LOG("Driver history: the game turned dispatch's line down.");
                ShowHiddenTakes();
                return;
            }
            gHistoryTracked = true;
            LOG("Driver history: dispatch reads out record tier %u.", tier + 1u);
        }

        void AnswerHistoryRequest(uint32_t now) {
            if (now < gHistoryAskedAt || now - gHistoryAskedAt > kHistoryWindow) {
                LOG("Driver history: the pilot's history request never played, so dispatch stays quiet.");
                gHistoryAsked = false;
                return;
            }
            if (PlayingEntry() == kUnit911Reply.entry) {
                gHistoryHeard = true;
                return;
            }
            if (!gHistoryHeard || RadioHeld(now)) return;
            gHistoryAsked = false;
            gHistoryHeard = false;
            if (HeatLevel() < kHistoryMinHeat) {
                LOG("Driver history: the pilot asked for your history below heat %d, so dispatch stays quiet.", kHistoryMinHeat);
                return;
            }
            LOG("Driver history: the pilot asked for your history, so dispatch reads it out.");
            DispatchHistory(now);
        }

        void FollowHistoryLine(uint32_t now) {
            if (!gHistoryTracked || Track(gHistoryLine, now) == LineState::Waiting) return;
            gHistoryTracked = false;
            ShowHiddenTakes();
        }

        void FinishBackupReply() {
            if (gBackupReplying && gCheckInStep == CheckInStep::Idle) RestoreRule(gUnit911ReplyLeadIns);
            gBackupReplying = false;
            gBackupStep = BackupStep::Idle;
        }

        void ReplyToBackup(uint32_t dispatcher, uint32_t now) {
            gBackupStep = BackupStep::Idle;
            void* heli = HeliRadio();
            const bool up = heli && !gHeliDown;
            if ((up && !gSwarmingOn) || (!up && (!dispatcher || (HeatLevel() < kHeliMinHeat && gHelisThisPursuit == 0)))) return;
            if (static_cast<unsigned>(std::rand()) % kBackupReplyChance != 0) {
                LOG("Backup reply: the pilot let this call for backup go.");
                return;
            }
            void* event = nullptr;
            if (up) {
                ResetPlayCount(kHeliSwarming);
                event = OwnLine([heli] { return reinterpret_cast<RadioCall>(kHeliSwarming.function)(heli, nullptr); });
            } else {
                ResetPlayCount(kUnit911Reply);
                RelaxRule(gUnit911ReplyLeadIns, now);
                gBackupReplying = true;
                const uint32_t params[] = { kHeliVoice };
                event = OwnLine([&params, dispatcher] { return QueueEvent(kUnit911Reply, params, Pointer(dispatcher)); });
            }
            if (!Listen(gBackupReplyLine, event, now)) {
                LOG("Backup reply: the game turned the pilot's line down.");
                FinishBackupReply();
                return;
            }
            gBackupStep = BackupStep::Pilot;
            if (up)
                LOG("Backup reply: the pilot tells the unit he can see its cover closing in.");
            else
                LOG("Backup reply: no helicopter is up, so the pilot answers on his way in.");
        }

        bool IsBackupCall(uint32_t entry) {
            return entry == kInitialCallForBU || entry == kCallForBackup.entry;
        }

        void FollowBackupCalls(uint32_t now) {
            const uint32_t dispatcher = Dispatcher();
            if (gBackupStep == BackupStep::Pilot) {
                if (Track(gBackupReplyLine, now) != LineState::Waiting) FinishBackupReply();
                return;
            }
            if (gBackupStep == BackupStep::Dispatch) {
                const LineState line = Track(gBackupReplyLine, now);
                if (line == LineState::Heard) ReplyToBackup(dispatcher, now);
                else if (line == LineState::Dropped) gBackupStep = BackupStep::Idle;
                return;
            }

            uint32_t entry = 0;
            uint32_t caller = 0;
            uint32_t voice = 0;
            const uint32_t playing = PlayingEvent();
            const bool known = playing && Read(playing, &entry, sizeof(entry)) && Read(playing + kEventSpeaker, &caller, sizeof(caller));
            if (known && IsBackupCall(entry) && caller && Read(caller + kRadioVoice, &voice, sizeof(voice)) && voice != kHeliVoice) {
                gBackupStep = BackupStep::Called;
                gBackupAnswered = false;
                gBackupHeardAt = now;
                return;
            }
            if (gBackupStep != BackupStep::Called) return;
            if (known && dispatcher && caller == dispatcher) {
                gBackupAnswered = true;
                gBackupHeardAt = now;
                return;
            }
            if (now < gBackupHeardAt || now - gBackupHeardAt > kBackupWindow) {
                gBackupStep = BackupStep::Idle;
                return;
            }
            if (playing || RadioBusy() || RadioHeld(now) || gCheckInStep != CheckInStep::Idle) return;
            if (gBackupAnswered || !dispatcher) {
                ReplyToBackup(dispatcher, now);
                return;
            }
            ResetPlayCount(kDispBUETA);
            void* event = OwnQueued([dispatcher] { return reinterpret_cast<RadioCall>(kDispBUETA.function)(Pointer(dispatcher), nullptr); });
            if (!Listen(gBackupReplyLine, event, now)) {
                ReplyToBackup(dispatcher, now);
                return;
            }
            LOG("Backup reply: dispatch never answered the unit, so she tells it backup is on the way.");
            gBackupStep = BackupStep::Dispatch;
        }

        void* __fastcall HeliSwarming(void* speaker, void*) {
            if (HeliRadio() && !gHeliDown) return reinterpret_cast<RadioCall>(kHeliSwarming.function)(speaker, nullptr);
            LOG("Swarming: the helicopter is down or gone, so the pilot doesn't report cover units closing in.");
            return nullptr;
        }

        void NoteDispatchQuestion(unsigned size, const void* params, uintptr_t entry, void* event, uint32_t now) {
            if (!gUpdateReplyOn || !event || entry != kDispPursuitUpdate || size < 3u * sizeof(uint32_t)) return;
            void* heli = HeliRadio();
            uint32_t callsign[2] = {};
            uint32_t asked[2] = {};
            if (!heli || !Read(Address(heli) + kRadioBattalion, &callsign[0], sizeof(callsign[0]))
                || !Read(Address(heli) + kRadioUnitNumber, &callsign[1], sizeof(callsign[1]))
                || !Read(reinterpret_cast<uintptr_t>(params) + sizeof(uint32_t), asked, sizeof(asked))
                || asked[0] != callsign[0] || asked[1] != callsign[1])
                return;
            LOG("Pursuit update: dispatch is asking the helicopter (callsign %lu/%lu) for an update.",
                static_cast<unsigned long>(callsign[0]), static_cast<unsigned long>(callsign[1]));
            gHeliAsked = true;
            gHeliQuestion = { Address(event), now, false };
        }

        void AnswerDispatchQuestion(uint32_t now) {
            const LineState question = Track(gHeliQuestion, now);
            if (question == LineState::Waiting && now >= gHeliQuestion.queued && now - gHeliQuestion.queued <= kUpdateReplyWindow) return;
            gHeliAsked = false;
            if (question != LineState::Heard) {
                LOG("Pursuit update: dispatch's question to the helicopter never played, so the pilot stays on his usual updates.");
                return;
            }
            void* heli = HeliRadio();
            if (!heli || gHeliDown) return;
            if (gDrivingLineSaid && now >= gDrivingLineAt && now - gDrivingLineAt < kDrivingLineGap) {
                LOG("Pursuit update: the pilot described your driving less than a minute ago, so he gives his usual update.");
                return;
            }
            if (HeatLevel() < kDrivingLineMinHeat || SuspectSpeed() < kDrivingLineMinMph) {
                LOG("Pursuit update: too slow or too little heat for the driving lines, so the pilot gives his usual update.");
                return;
            }
            ResetPlayCount(kSuspectBehaviour);
            const uint32_t params[] = { SpeakerVoice(heli), kSingleSuspect };
            void* event = OwnLine([&params, heli] { return QueueEvent(kSuspectBehaviour, params, heli); });
            if (!Listen(gUpdateReplyLine, event, now)) {
                LOG("Pursuit update: the game turned the driving lines down, so the pilot gives his usual update.");
                return;
            }
            LOG("Pursuit update: dispatch asked the helicopter, so the pilot answers by describing your driving.");
            gUpdateReplyTracked = true;
            gDrivingLineSaid = true;
            gDrivingLineAt = now;
        }

        void* __fastcall HeliPursuitUpdateRep(void* speaker, void*) {
            uint32_t now = 0;
            if (Now(now)) {
                if (gUpdateReplyTracked && Track(gUpdateReplyLine, now) == LineState::Waiting) return nullptr;
                if (gHeliAsked && now >= gHeliQuestion.queued && now - gHeliQuestion.queued <= kUpdateReplyWindow
                    && Track(gHeliQuestion, now) != LineState::Dropped)
                    return nullptr;
            }
            return reinterpret_cast<RadioCall>(kPursuitUpdateRep.function)(speaker, nullptr);
        }

        void* __fastcall HeliSuspectBehaviour(void*, void*) {
            const uint32_t speech = SpeechModule();
            void* unit = speech ? reinterpret_cast<ClosestVoiceCall>(kClosestVoice)(speech, nullptr, 1, 0) : nullptr;
            if (!unit) {
                LOG("Driving lines: the game picked the pilot and no ground unit is close, so nobody says one.");
                return nullptr;
            }
            LOG("Driving lines: the game picked the pilot, so the closest ground unit describes your driving instead.");
            return CallRadio(unit, kDrivingLineSlot);
        }

        void BroadcastThemeChange(uint32_t now) {
            const uint32_t dispatcher = Dispatcher();
            if (!dispatcher) return;
            if (!HideTakes(kBroadcastSample, kDispatcherVoice, kHiddenBroadcastTake, 1, kMultipleSuspectsTag, now))
                LOG("Pursuit theme: ENTRY_05440 couldn't be hidden, so all four multiple-vehicles takes can play.");
            ResetPlayCount(kPursuitBroadcast);
            const uint32_t params[] = { SpeakerVoice(Pointer(dispatcher)), kMultipleSuspects };
            void* event = OwnLine([&params, dispatcher] { return QueueEvent(kPursuitBroadcast, params, Pointer(dispatcher)); });
            if (!Listen(gBroadcastLine, event, now)) {
                LOG("Pursuit theme: the game turned dispatch's broadcast down.");
                ShowHiddenTakes();
                return;
            }
            gBroadcastTracked = true;
            LOG("Pursuit theme: dispatch makes her multiple-vehicles broadcast.");
        }

        void NoteThemeChange(float pursuit, uint32_t now) {
            int theme = 0;
            if (!Read(kPursuitTheme, &theme, sizeof(theme))) return;
            if (!gThemeKnown || pursuit < kThemeSettleSeconds) {
                gPursuitTheme = theme;
                gThemeKnown = true;
                return;
            }
            if (theme == gPursuitTheme) return;
            gPursuitTheme = theme;
            LOG("Pursuit theme: the music moved on to theme %d.", theme + 1);
            if (!HeliRadio() || gHeliDown) {
                LOG("Pursuit theme: no helicopter is up, so the radio waits for the next theme change.");
                return;
            }
            if (gBroadcastTracked) return;
            BroadcastThemeChange(now);
        }

        void FollowBroadcast(uint32_t now) {
            if (!gBroadcastTracked) return;
            const LineState broadcast = Track(gBroadcastLine, now);
            if (broadcast != LineState::Waiting) {
                gBroadcastTracked = false;
                ShowHiddenTakes();
            }
            if (broadcast == LineState::Heard) {
                LOG("Pursuit theme: dispatch finished her broadcast.");
                gVehicleReportDue = gVehicleReportOn;
                gVehicleReportAt = now;
            }
        }

        void ReportVehicle(void* speaker, uint32_t now) {
            if (!gVehicleReportDue) return;
            if (now < gVehicleReportAt || now - gVehicleReportAt > kVehicleReportWindow) {
                gVehicleReportDue = false;
                return;
            }
            if (!HasVisual(speaker) || RadioHeld(now)) return;
            gVehicleReportDue = false;
            void* event = OwnQueued([speaker] { return CallRadio(speaker, kVehicleReportSlot); });
            Listen(gVehicleLine, event, now);
            LOG("Vehicle report: dispatch's theme-change broadcast is over, so the pilot describes your car (%s).",
                event ? "queued" : "turned down by the game");
        }

        void ReportWeather(void* speaker, uint32_t now) {
            const float rain = RainLevel();
            if (!(rain > 0.0f)) {
                gRainReported = false;
                return;
            }
            if (gRainReported || !(rain > kRainReportLevel) || !HasVisual(speaker)) return;
            gRainReported = true;
            ResetPlayCount(kWeatherReport);
            void* event = OwnLine([speaker] { return reinterpret_cast<RadioCall>(kWeatherReport.function)(speaker, nullptr); });
            if (Listen(gWeatherLine, event, now)) static_cast<uint8_t*>(event)[kEventPriority] = kInterruptPriority;
            LOG("Weather: it started raining, so the pilot cuts in about the weather (%s).", event ? "queued" : "turned down by the game");
        }

        void HelicopterArrived(uint32_t now) {
            LOG("Helicopter: a helicopter joined the chase.");
            gHeliJoinedAt = now;
            gRoadblockAsked = false;
            gAirSupportWanted = false;
            gAirSupportAsked = false;
            const bool respawn = gHelisThisPursuit++ > 0;
            if (gAirSupportStep == AirSupportStep::Request) {
                LOG("Air support: a helicopter showed up before anyone asked, so nobody asks.");
                FinishAirSupport(now, false);
            }
            if (respawn) {
                if (gAirSupportStep != AirSupportStep::Ready) FinishAirSupport(now, false);
                LOG("Helicopter: a new helicopter is in the chase, so the pilot cuts in to say he has you.");
                gSpotPending = true;
                gSpotInterrupt = true;
                gSpotVanilla = 0;
                gSpotAt = now;
                return;
            }
            StartCheckIn(now);
        }

        void WatchHelicopter(void* speaker, uint32_t now) {
            uint32_t heli = 0;
            const bool heliOut = Read(kHeliVehicle, &heli, sizeof(heli)) && heli;
            if (heliOut && (!gHeliOut || heli != gHeliSeen)) {
                gCheckInStep = CheckInStep::Idle;
                gTunnelAlertPending = false;
                gHeliDown = false;
                gHeliAlive = false;
                gBailoutPending = false;
                RestoreRule(gUnit911ReplyLeadIns);
                HelicopterArrived(now);
            }
            if (gHeliOut && !heliOut) {
                LOG("Helicopter: it left the chase, so a unit will ask for another one when it can see you.");
                gAirSupportWanted = true;
                gAirSupportAsked = false;
                gAirSupportFailures = 0;
            }
            gHeliOut = heliOut;
            gHeliSeen = heli;

            const bool destroyed = Destroyed(speaker);
            if (heliOut && !destroyed) gHeliAlive = true;
            if (destroyed && gHeliAlive && !gHeliDown) {
                gHeliAlive = false;
                gHeliDown = true;
                gTunnelAlertPending = false;
                LOG("Going down: the helicopter was destroyed.");
                ReportDamage(speaker, now);
            }
        }

        void* __fastcall HeliRadioUpdate(void* speaker, void*) {
            void* result = reinterpret_cast<RadioCall>(kRadioUpdate)(speaker, nullptr);
            uint32_t now = 0;
            if (!Now(now)) return result;
            ResolveBailout(now);
            WatchHelicopter(speaker, now);
            if (!gHeliOut || gHeliDown) return result;
            CheckIn(speaker, now);
            TunnelAlert(speaker, now);
            PendingSpot(speaker, now);
            if (gRoadblockOn) AnswerRoadblockRequest(speaker, now);
            if (gWeatherOn) ReportWeather(speaker, now);
            if (gVehicleReportOn) ReportVehicle(speaker, now);
            return result;
        }

        void EndPursuit(uint32_t now) {
            if (gAirSupportStep != AirSupportStep::Ready) FinishAirSupport(now, false);
            if (gRoadblockStep != RoadblockStep::Ready) FinishRoadblock();
            gAirSupportWanted = false;
            gAirSupportAsked = false;
            gRoadblockRequested = false;
            gHelisThisPursuit = 0;
            gThemeKnown = false;
            gBroadcastTracked = false;
            gVehicleReportDue = false;
            gHistoryTracked = false;
            ShowHiddenTakes();
            if (gBackupStep != BackupStep::Idle) FinishBackupReply();
            gHeliAsked = false;
            gSpotPending = false;
            gSpotInterrupt = false;
            gSpotSaid = false;
        }

        void PursuitTick(uint32_t now) {
            ResolveBailout(now);
            const float pursuit = PursuitTime();
            if (pursuit < 0.0f) {
                EndPursuit(now);
                return;
            }
            if (!(RainLevel() > 0.0f)) gRainReported = false;
            if (gThemeOn) NoteThemeChange(pursuit, now);
            FollowBroadcast(now);
            if (gHistoryAsked && !gHistoryTracked) AnswerHistoryRequest(now);
            if (gBackupReplyOn) FollowBackupCalls(now);
            if (gHeliAsked) AnswerDispatchQuestion(now);
            FollowHistoryLine(now);
            if (gHiddenCount && (now < gHiddenAt || now - gHiddenAt > kHiddenTakeTimeout)) ShowHiddenTakes();
            if (gRoadblockRequested && !HeliRadio()) {
                LOG("Roadblock: no helicopter is out, so the ground units handle it.");
                gRoadblockRequested = false;
            }
            if (gAirSupportWanted && gAirSupportStep == AirSupportStep::Ready && gRoadblockStep == RoadblockStep::Ready && !HeliRadio()
                && !RadioBusy() && PickAirSupportCaller())
                StartAirSupport(now);
            AdvanceAirSupport(now);
            if (gRoadblockOn) AdvanceRoadblock(now);
        }

        void* __fastcall CopRadioUpdate(void* speaker, void*) {
            void* result = reinterpret_cast<RadioCall>(kCopRadioUpdate)(speaker, nullptr);
            uint32_t now = 0;
            if (Now(now) && now != gLastPursuitTick) {
                gLastPursuitTick = now;
                PursuitTick(now);
            }
            return result;
        }

        void* __cdecl QueueEventHook(unsigned size, const void* params, uintptr_t entry, uintptr_t binding, void* speaker) {
            uint32_t now = 0;
            const bool clock = Now(now);
            RestoreExpiredRules(clock, now);
            if (!gOwnLine && clock && RadioHeld(now)) return nullptr;
            void* event = reinterpret_cast<QueueEventCall>(gQueueEventOriginal)(size, params, entry, binding, speaker);
            if (gOwnLine) gOwnQueued = event;
            else if (clock) NoteDispatchQuestion(size, params, entry, event, now);
            return event;
        }

        bool Verify(const Event& event) {
            uint32_t entry[2] = {};
            char name[64] = "";
            const size_t length = std::strlen(event.name) + 1;
            if (length > sizeof(name) || !Read(event.entry, entry, sizeof(entry)) || entry[1] != event.id
                || !Read(entry[0], name, length) || std::memcmp(name, event.name, length) != 0) {
                LOG("%s is off: speed.exe's speech event list does not have it at 0x%08lX.", event.name, static_cast<unsigned long>(event.entry));
                return false;
            }

            uint8_t code[24] = {};
            uint8_t binding[5] = { kPush };
            uint8_t listed[5] = { kPush };
            const uint32_t bindingAddress = static_cast<uint32_t>(event.binding);
            const uint32_t entryAddress = static_cast<uint32_t>(event.entry);
            std::memcpy(binding + 1, &bindingAddress, sizeof(bindingAddress));
            std::memcpy(listed + 1, &entryAddress, sizeof(entryAddress));
            const bool found = Read(event.function + event.pushes, code, sizeof(code)) && std::memcmp(code, binding, sizeof(binding)) == 0;
            bool pushed = false;
            for (size_t i = sizeof(binding); found && !pushed && i + sizeof(listed) <= sizeof(code); ++i)
                pushed = std::memcmp(code + i, listed, sizeof(listed)) == 0;
            if (!pushed) {
                LOG("%s is off: the game's code at 0x%08lX is not what this mod expects.", event.name, static_cast<unsigned long>(event.function));
                return false;
            }
            return true;
        }

        bool VerifySlot(const Event& event, uintptr_t slot) {
            uint32_t function = 0;
            if (Verify(event) && Read(slot, &function, sizeof(function)) && function == event.function) return true;
            LOG("%s is off: the police radio does not use the game's own function for it.", event.name);
            return false;
        }

        bool Replace(const Event& event, uintptr_t va, const void* expected, const void* replacement, size_t length) {
            if (gPatchCount >= static_cast<int>(sizeof(gPatches) / sizeof(gPatches[0]))) return false;
            Patch& patch = gPatches[gPatchCount];
            if (length > sizeof(patch.original) || !Read(va, patch.original, length) || std::memcmp(patch.original, expected, length) != 0) {
                LOG("%s is off: another mod already changed the game's code at 0x%08lX.", event.name, static_cast<unsigned long>(va));
                return false;
            }
            if (!WriteCode(va, replacement, length)) {
                LOG("%s is off: the game's code at 0x%08lX could not be changed.", event.name, static_cast<unsigned long>(va));
                return false;
            }
            patch.va = va;
            patch.length = length;
            ++gPatchCount;
            return true;
        }

        bool Redirect(const Event& event, uintptr_t va, uint32_t expected, uint32_t replacement) {
            return Replace(event, va, &expected, &replacement, sizeof(replacement));
        }

        bool RedirectCall(const Event& event, uintptr_t call, uint32_t original, uint32_t replacement) {
            uint8_t opcode = 0;
            if (!Read(call, &opcode, sizeof(opcode)) || opcode != kCall) {
                LOG("%s is off: another mod already changed the game's code at 0x%08lX.", event.name, static_cast<unsigned long>(call));
                return false;
            }
            return Redirect(event, call + 1, CallOffset(call, original), CallOffset(call, replacement));
        }

        bool ReplaceSlot(const Event& event, uintptr_t slot, uint32_t replacement) {
            return VerifySlot(event, slot) && Redirect(event, slot, static_cast<uint32_t>(event.function), replacement);
        }

        bool Detour(const Event& event, uintptr_t function, const uint8_t* code, size_t length, uint32_t replacement, uintptr_t& original) {
            uint8_t* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (!trampoline) return false;
            const uint32_t back = CallOffset(reinterpret_cast<uintptr_t>(trampoline) + length, static_cast<uint32_t>(function + length));
            std::memcpy(trampoline, code, length);
            trampoline[length] = kJump;
            std::memcpy(trampoline + length + 1, &back, sizeof(back));
            FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5);

            uint8_t jump[8] = { kJump };
            const uint32_t offset = CallOffset(function, replacement);
            std::memcpy(jump + 1, &offset, sizeof(offset));
            for (size_t i = 5; i < length; ++i) jump[i] = kNop;
            const uintptr_t previous = original;
            original = reinterpret_cast<uintptr_t>(trampoline);
            if (Replace(event, function, code, jump, length)) return true;
            original = previous;
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        bool HookCall(uintptr_t call, uint32_t original, uint32_t replacement) {
            if (!CallsTo(call, original)) return false;
            const uint32_t offset = CallOffset(call, replacement);
            return WriteCode(call + 1, &offset, sizeof(offset));
        }

        void UnhookCall(uintptr_t call, uint32_t original, bool& hooked) {
            if (!hooked) return;
            const uint32_t offset = CallOffset(call, original);
            WriteCode(call + 1, &offset, sizeof(offset));
            hooked = false;
        }

        bool HookQueueEvent() {
            return Detour(kDispBackupReply, kQueueEvent, kQueueEventCode, sizeof(kQueueEventCode), Address(&QueueEventHook), gQueueEventOriginal);
        }

        bool HookHeliRadioUpdate() {
            return Redirect(kSuspectBehaviour, kRadioUpdateSlot, static_cast<uint32_t>(kRadioUpdate), Address(&HeliRadioUpdate));
        }

        bool HookCopRadioUpdate() {
            return VerifySlot(kCallForBackup, kCopRadioVtable + kCallForBackupSlot) && Verify(kDispBackupReply)
                && Redirect(kCallForBackup, kCopRadioUpdateSlot, static_cast<uint32_t>(kCopRadioUpdate), Address(&CopRadioUpdate));
        }

        bool HookSupportRequests() {
            return Verify(kCallForRoadblock)
                && Detour(kCallForRoadblock, kSupportRequest, kSupportRequestCode, sizeof(kSupportRequestCode), Address(&SupportRequestHook),
                          gSupportRequestOriginal);
        }

        bool HookLostSuspect() {
            return Verify(kLostSuspect)
                && RedirectCall(kLostSuspect, kLostSuspectCall, static_cast<uint32_t>(kLostSuspect.function), Address(&HeliLostSuspect));
        }

        bool RemoveRepeatFilter() {
            return Replace(kDispBackupReply, kRepeatFilter, kRepeatFilterCode, kAlwaysRepeat, sizeof(kAlwaysRepeat));
        }

        bool GameCodeMatches(uintptr_t va, const uint8_t* expected, size_t length) {
            uint8_t code[64] = {};
            return length <= sizeof(code) && Read(va, code, length) && std::memcmp(code, expected, length) == 0;
        }

    }

    bool Init(void* module) {
        OpenLog(static_cast<HMODULE>(module));
        LOG("NFSMWRevisedAirSupport %s starting.", kVersion);
        std::srand(GetTickCount());
        LoadSpeechEntries();

        const bool intentToRam = ReplaceSlot(kIntentToRam, kIntentToRamSlot, Address(&HeliIntentToRam));
        if (intentToRam)
            LOG("%s restored: the pilot switches between the unused normal takes and the game's intense ones when he goes in to ram you.",
                kIntentToRam.name);

        const bool strategyReset = ReplaceSlot(kStrategyReset, kStrategyResetSlot, Address(&HeliStrategyReset));
        if (strategyReset)
            LOG("%s restored: the pilot alternates the 'go again' and 'hold on station' takes when he calls a reset.", kStrategyReset.name);

        const bool lostSuspect = HookLostSuspect();
        if (lostSuspect)
            LOG("%s restored: the pilot calls it when the police lose you while a helicopter is out.", kLostSuspect.name);

        if (GameCodeMatches(kCollisionReaction, kCollisionReactionCode, sizeof(kCollisionReactionCode)))
            gHitReturn = kCollisionReaction + sizeof(kCollisionReactionCode);
        gBailoutReasonOn = GameCodeMatches(kFuelCheck, kFuelCheckCode, sizeof(kFuelCheckCode)) && CallsTo(kBailoutTakesCall, static_cast<uint32_t>(kBailoutTakes));
        const bool bailout = ReplaceSlot(kHeliBailout, kHeliBailoutSlot, Address(&HeliBailout));
        if (bailout && gBailoutReasonOn)
            LOG("%s fixed: a helicopter that leaves with fuel left reports damage or a search pattern, not low fuel.", kHeliBailout.name);
        const bool tunnelAlert = bailout && ReplaceSlot(kHazardAlert, kHazardAlertSlot, Address(&HeliHazardAlert));
        if (tunnelAlert)
            LOG("%s fixed: the pilot gives no hazard warnings after his going-down call.", kHazardAlert.name);

        const bool regainVisual = ReplaceSlot(kRegainVisual, kRegainVisualSlot, Address(&HeliRegainsVisual));
        if (regainVisual)
            LOG("%s restored: when the pilot finds you again, he switches between his unused takes and the game's own.", kRegainVisual.name);
        const bool spotted = regainVisual && ReplaceSlot(kSpotted, kSpottedSlot, Address(&HeliSpotted));
        if (spotted)
            LOG("%s: when the pilot first spots you, he also switches between those unused takes and the game's own.", kSpotted.name);

        const bool alwaysRepeat = RemoveRepeatFilter();
        if (alwaysRepeat)
            LOG("Police radio: lines that already played can keep playing later in long pursuits.");

        const bool radioUpdate = HookHeliRadioUpdate();
        const bool queueHooked = radioUpdate && HookQueueEvent();

        gAirSupportOn = queueHooked && HookCopRadioUpdate();
        if (gAirSupportOn)
            LOG("%s restored: when a helicopter goes down, a unit that can see you asks for another one on the radio.", kCallForBackup.name);

        gCheckInOn = queueHooked && VerifySlot(kUnit911Reply, kUnit911ReplySlot);
        if (gCheckInOn)
            LOG("%s restored: the pilot checks in when the first helicopter joins.", kUnit911Reply.name);

        gRoadblockOn = gAirSupportOn && Verify(kRoadblockUpdate) && HookSupportRequests();
        if (gRoadblockOn)
            LOG("%s restored: when the police set up a roadblock and a helicopter has been out long enough, the pilot calls it and dispatch answers.",
                kCallForRoadblock.name);

        gTakesHooked = HookCall(kTakeOffsetCall, static_cast<uint32_t>(kTakeOffset), Address(&TakeLoadedHook));
        if (!gTakesHooked)
            LOG("Speech player: another mod changed the game's take lookup, so take-based calls are off.");
        gHistoryOn = gTakesHooked && queueHooked && Verify(kDriverHistory);
        if (gHistoryOn)
            LOG("%s restored: when the pilot asks for your history at heat 5 and up, dispatch reads out your record.", kDriverHistory.name);

        gSwarmingOn = ReplaceSlot(kHeliSwarming, kHeliSwarmingSlot, Address(&HeliSwarming));
        if (gSwarmingOn)
            LOG("%s fixed: the pilot only reports cover units closing in while his helicopter is up.", kHeliSwarming.name);

        gTakeChoiceHooked = gTakesHooked && gCheckInOn && HookCall(kTakeCheckCall, static_cast<uint32_t>(kTakeCheck), Address(&TakeCheckHook));
        if (gTakesHooked && gCheckInOn && !gTakeChoiceHooked)
            LOG("Speech player: another mod changed how the game picks takes, so the pilot's backup reply is off.");
        gBackupReplyOn = gTakeChoiceHooked && Verify(kDispBUETA);
        if (gBackupReplyOn)
            LOG("%s: after a unit calls for backup and dispatch answers, the pilot now and then answers too.", kCallForBackup.name);

        gUpdateReplyOn = queueHooked && VerifySlot(kSuspectBehaviour, kSuspectBehaviourSlot)
                         && ReplaceSlot(kPursuitUpdateRep, kPursuitUpdateRepSlot, Address(&HeliPursuitUpdateRep));
        if (gUpdateReplyOn)
            LOG("%s restored: when dispatch asks the helicopter for an update, the pilot describes your driving.", kPursuitUpdateRep.name);
        const bool drivingLines = gUpdateReplyOn && ReplaceSlot(kSuspectBehaviour, kSuspectBehaviourSlot, Address(&HeliSuspectBehaviour));
        if (drivingLines)
            LOG("%s fixed: the pilot only describes your driving when dispatch asks him; the game's own driving calls go to ground units.",
                kSuspectBehaviour.name);

        gThemeOn = queueHooked && Verify(kPursuitBroadcast);
        if (gThemeOn)
            LOG("%s: when the pursuit music moves to the next theme and a helicopter is up, dispatch makes a broadcast.", kPursuitBroadcast.name);
        gVehicleReportOn = queueHooked && VerifySlot(kVehicleReport, kHeliRadioVtable + kVehicleReportSlot);
        if (gVehicleReportOn)
            LOG("%s restored: after dispatch's theme-change broadcast, the pilot describes your car.", kVehicleReport.name);

        gWeatherOn = radioUpdate && VerifySlot(kWeatherReport, kWeatherReportSlot);
        if (gWeatherOn)
            LOG("%s restored: when it starts raining, the pilot cuts in about the weather.", kWeatherReport.name);

        return intentToRam || strategyReset || lostSuspect || bailout || regainVisual || alwaysRepeat || gAirSupportOn || gCheckInOn
            || gRoadblockOn || gHistoryOn || gSwarmingOn || gBackupReplyOn || gUpdateReplyOn || gThemeOn || gVehicleReportOn || gWeatherOn;
    }

    void Restore() {
        ShowHiddenTakes();
        UnhookCall(kTakeCheckCall, static_cast<uint32_t>(kTakeCheck), gTakeChoiceHooked);
        UnhookCall(kTakeOffsetCall, static_cast<uint32_t>(kTakeOffset), gTakesHooked);
        RestoreAllRules();
        while (gPatchCount > 0) {
            const Patch& patch = gPatches[--gPatchCount];
            WriteCode(patch.va, patch.original, patch.length);
        }
        CloseLog();
    }

}
