#pragma once
#include <cstdint>

struct HSIMABLE__;
typedef HSIMABLE__* HSIMABLE;

class WRoadNav;
class IVehicle;

namespace UMath {

    struct Vector3 {
        float x, y, z;
    };

}

class Timer {
  public:
    int PackedTime;
};

enum SPCHType_1_EventID {
    kSPCH1_EventID_CallForBU         = 67,
    kSPCH1_EventID_CallForRB         = 76,
    kSPCH1_EventID_DispRBUpdate      = 78,
    kSPCH1_EventID_StrategyReset     = 94,
    kSPCH1_EventID_DispPursuitUpdate = 99,
    kSPCH1_EventID_PursuitUpdateRep  = 100,
    kSPCH1_EventID_Unit911Reply      = 103,
    kSPCH1_EventID_RegainVisual      = 107,
    kSPCH1_EventID_LostSuspect       = 108,
    kSPCH1_EventID_IntentToRam       = 113,
    kSPCH1_EventID_SuspectBehaviour  = 116,
    kSPCH1_EventID_DriverHistory     = 118,
    kSPCH1_EventID_VehicleReport     = 155,
    kSPCH1_EventID_InitialCallForBU  = 161,
    kSPCH1_EventID_HeliBailout       = 176,
    kSPCH1_EventID_HeliSwarming      = 177,
    kSPCH1_EventID_HeliHazardAlert   = 179,
    kSPCH1_EventID_DispPursEscGen    = 181,
    kSPCH1_EventID_DispBackupReply   = 184,
    kSPCH1_EventID_Spotted           = 214,
    kSPCH1_EventID_WeatherReport     = 219,
    kSPCH1_EventID_DispBUETA         = 228,
};

namespace Csis {

    struct InterfaceId {
        const char* pString;
        short       systemCrc;
        short       interfaceCrc;
    };

    class FunctionHandle {
      public:
        void* mpPrivate;
        int   mKey;
    };

    enum Type_code : int {};
    enum Type_heat_level : int {};

    enum Type_intensity {
        Type_intensity_Normal = 1,
        Type_intensity_High   = 2,
    };

    enum Type_heli_bailout_type {
        Type_heli_bailout_type_flight_conditions = 1,
        Type_heli_bailout_type_low_ammo          = 2,
        Type_heli_bailout_type_fuel_low          = 4,
        Type_heli_bailout_type_damage_sustained  = 8,
    };

    enum Type_heli_hazard_alert_type {
        Type_heli_hazard_alert_type_windy_roads         = 1,
        Type_heli_hazard_alert_type_approaching_highway = 2,
        Type_heli_hazard_alert_type_approaching_tunnel  = 4,
        Type_heli_hazard_alert_type_approaching_city    = 8,
        Type_heli_hazard_alert_type_approaching_airport = 16,
        Type_heli_hazard_alert_type_approaching_blimp   = 32,
    };

    enum Type_yes_no {
        Type_yes_no_Yes_True = 1,
        Type_yes_no_No_False = 2,
    };

    enum Type_roadblock_type {
        Type_roadblock_type_Roadblock_Generic_ = 1,
        Type_roadblock_type_Spikes             = 2,
    };

    enum Type_region {
        Type_region_College_Town = 1,
        Type_region_Coastal      = 2,
        Type_region_City         = 4,
        Type_region_Alpine       = 8,
    };

    enum Type_num_suspects {
        Type_num_suspects_one_suspect       = 1,
        Type_num_suspects_multiple_suspects = 2,
    };

    enum Type_disp_backup_type {
        Type_disp_backup_type_Air_Support = 8,
    };

    struct AnytimeEvents_IntentToRamStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct AnytimeEvents_LostSuspectStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct AnytimeEvents_RegainVisualStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct HeliSpecific_HeliBailoutStruct {
        int                    speaker_id;
        Type_heli_bailout_type heli_bailout_type;
    };

    struct AnytimeEvents_Unit911ReplyStruct {
        int speaker_id;
    };

    struct StaticRoadblock_DispRBUpdateStruct {
        int                 speaker_id;
        Type_code           code;
        Type_yes_no         yes_no;
        Type_roadblock_type roadblock_type;
    };

    struct AnytimeEvents_DriverHistoryStruct {
        int         speaker_id;
        Type_region region;
    };

    struct AnytimeEvents_SuspectBehaviourStruct {
        int               speaker_id;
        Type_num_suspects num_suspects;
    };

    struct AnytimeEvents_DispPursEscGenStruct {
        int               speaker_id;
        Type_num_suspects num_suspects;
    };

    struct AnytimeEvents_WeatherReportStruct {
        int speaker_id;
    };

}

namespace Attrib {

    class Collection;

    class Instance {
      public:
        const void* GetAttributePointer(unsigned int attribkey, unsigned int index) const;

        void*             mOwner;
        const Collection* mCollection;
        void*             mLayoutPtr;
        unsigned int      mMsgPort;
        unsigned short    mFlags;
        unsigned short    mLocks;
    };

    const Collection* FindCollection(unsigned int classkey, unsigned int collectionkey);

}

namespace UTL {
namespace COM {

    class Object;

    class IUnknown {
      public:
        template <typename T> bool QueryInterface(T** out) {
            *out = static_cast<T*>(FindInterface(T::_IHandle()));
            return *out != nullptr;
        }

      protected:
        virtual ~IUnknown() {}

      private:
        IUnknown* FindInterface(void* handle);

        Object* _mCOMObject;
    };

}
}

class ISimable : public UTL::COM::IUnknown {
  public:
    static ISimable* FindInstance(HSIMABLE handle);
};

class IAIHelicopter : public UTL::COM::IUnknown {
  public:
    static void* _IHandle();

    virtual float GetDesiredHeightOverDest() const = 0;
    virtual void SetDesiredHeightOverDest(const float height) = 0;
    virtual void SetLookAtPosition(UMath::Vector3 la) = 0;
    virtual UMath::Vector3 GetLookAtPosition() const = 0;
    virtual void SetDestinationVelocity(const UMath::Vector3& v) = 0;
    virtual void SteerToNav(WRoadNav* road_nav, float height, float speed, bool bStopAtDest) = 0;
    virtual bool StartPathToPoint(UMath::Vector3& point) = 0;
    virtual bool StrafeToDestIsSet() const = 0;
    virtual void SetStrafeToDest(bool strafe) = 0;
    virtual bool FilterHeliAltitude(UMath::Vector3& point) = 0;
    virtual void RestrictPointToRoadNet(UMath::Vector3& seekPosition) = 0;
    virtual void SetFuelFull() = 0;
    virtual float GetFuelTimeRemaining() = 0;
};

class EAXCharacter;
class EAXCop;

class MReqBackup {
  public:
    int GetBackupType() const {
        return fBackupType;
    }

    unsigned char mMessage[0x10];
    int           fBackupType;
};

namespace Speech {

    enum SpeakerID {
        Dispatch       = 1,
        Heli           = 2,
        Primary1       = 3,
        Primary2       = 4,
        Primary3       = 5,
        Secondary1     = 6,
        Secondary2     = 7,
        Secondary3     = 8,
        Cross          = 9,
        NUM_SPEAKER_ID = 10,
    };

    struct Battalion {
        int name;
        int number;
    };

    struct ScheduledSpeechEvent {
        Csis::InterfaceId*    iid;
        Csis::FunctionHandle* fh;
        SPCHType_1_EventID    ID;
        EAXCharacter*         actor;
        Timer                 entry_time;
        Timer                 playback_time;
        Timer                 finish_time;
        void*                 assoc_samples[7];
        uint8_t               assoc_samples_count;
        uint8_t               assoc_samples_prep;
        uint8_t               curndx;
        uint8_t               priority;
        short                 frameindex;
        short                 flags;
    };

    struct History {
        Timer          time;
        unsigned short count;
        unsigned short speakers;
    };

    struct EventHistory {
        History* Find(SPCHType_1_EventID id);
    };

    struct Manager {
        static ScheduledSpeechEvent* ScheduleSpeechPartII(unsigned int size, void* data, Csis::InterfaceId& iid, Csis::FunctionHandle& fh,
                                                          EAXCharacter* actor);
        static void NotifyEventCompletion(ScheduledSpeechEvent* evt, bool playback_complete);
        static bool IsCopSpeechBusy();
        static bool IsQueued(SPCHType_1_EventID evtID, int indices);
        static bool IsCopSpeechPlaying(SPCHType_1_EventID event_id);

        static EventHistory& GetHistory() {
            return mGlobalHistory;
        }

        static void NotifyEventCompletionHook(ScheduledSpeechEvent* evt, bool playback_complete);

        static EventHistory& mGlobalHistory;
    };

    class StrategyFlow {
      public:
        void MessageReqBackup(const MReqBackup& message);
        void MessageReqBackupHook(const MReqBackup& message);
    };

    struct copPair {
        HSIMABLE hsimable;
        EAXCop*  cop;
    };

    class copMap {
      public:
        void*    mAllocator;
        copPair* mBegin;
        copPair* mEnd;
        copPair* mCapacityEnd;
    };

}

class MiscSpeech {
  public:
    static int LostSuspect(int spkrID);
    static int LostSuspectHook(int spkrID);
};

class EAXCharacter {
  public:
    virtual ~EAXCharacter();

    virtual void Ack();
    virtual void Deny();
    virtual void InterruptStatic();
    virtual void InterruptExpletive();
    virtual void InterruptViolent();
    virtual void InterruptComposedLow();
    virtual void InterruptComposedHigh();
    virtual void DriverHistory();
    virtual void HeatJump(Csis::Type_heat_level heat);
    virtual HSIMABLE GetHandle();
    virtual void SetHandle(HSIMABLE handle);
    virtual int GetSpeakerID();
    virtual int GetCallsign();
    virtual int GetUnitNumber();
    virtual void SetCallsign(int callsign);
    virtual void SetUnitNumber(int unitnum);
    virtual void SetSpeakerID(int spkrID);
    virtual void SetPosition(const UMath::Vector3& v);
    virtual const UMath::Vector3 GetPosition();
    virtual void SetSpeed(const float speed);
    virtual void Update();
    virtual float GetDistance();
    virtual float GetHealth();
    virtual bool IsActive();
    virtual void SetActive(bool active);
    virtual float GetSpeed();
    virtual bool IsDead();
    virtual bool HasLOS();
    virtual void SetLOS(bool yes);
    virtual Csis::Type_code GetRandomizedCode();
    virtual void Reset();

    int               mTimeLastSpoken;
    int               mLastEvent;
    int               mSpeakerID;
    HSIMABLE          mHandle;
    Speech::Battalion mCallsign;
    UMath::Vector3    mPos;
    float             mSpeed;
    float             mDistance;
    float             mHealth;
    bool              mDestroyed;
    bool              mActive;
    bool              mSuspectLOS;
};

class EAXCop : public EAXCharacter {
  public:
    virtual void AttemptVehicleStop();
    virtual void VehicleReport();
    virtual void InitiatePursuit();
    virtual void LocationReport();
    virtual void SelfStrategy(int type);
    virtual void InitialCallForBackup();
    virtual void CallForBackup(int type);
    virtual void UnitBackupReply();
    virtual void InitiateStrategy(int type);
    virtual void CallToPosition(EAXCop* cop);
    virtual void CallToPositionReminder();
    virtual void StrategyExecute();
    virtual void Collision(int collisionType, float force, EAXCop* spkr);
    virtual void AnticipateSuccess();
    virtual void AnticipateFail();
    virtual void OutcomeFail(short intensity);
    virtual void StrategyReset(bool new_strategy);
    virtual void SuspectBehavior();
    virtual void SuspectOutrun();
    virtual void SuspectUTurn();
    virtual void LostSuspect();
    virtual void LostVisual();
    virtual void RegainVisual();
    virtual void SwarmingReply();
    virtual void Arrest();
    virtual void PursuitUpdateReply();
    virtual void ReinitiatePursuit();
    virtual void NegativeBackupReply();
    virtual void BackupReminder(int type);
    virtual void BackupArrives();
    virtual void IntentToRam();
    virtual void CallforEV(unsigned int type);
    virtual void UnitDisabled(int other);
    virtual void Bailout();
    virtual void LoBailout();
    virtual void HiBailout();
    virtual void BailoutTraffic();
    virtual void BailoutBadRoad();
    virtual void Spotter();
    virtual void SpotterReply();
    virtual void Reply911();
    virtual void DenyBailout();
    virtual void PrimaryEngage();
    virtual void Bullhorn();
    virtual void PreBullhorn();
    virtual void BullhornArrest();
    virtual void SuspectConfirmed();
    virtual void FocusChange();
    virtual void Spotted();
    virtual void DirectionChange();
    virtual void CallForSwarming();
    virtual void SpotterWanted();
    virtual void Offroad(unsigned int id, bool subsequent);
    virtual void WeatherReport();
    virtual void CallForRB();
    virtual void RBReminder();
    virtual void NegRBReply();
    virtual void RBApproach();
    virtual void RBEngage(bool spikes_hit);
    virtual void PursuitApproaching();
    virtual void RBAverted();
    virtual void CallForSubRB();
    virtual void RearEnded(Csis::Type_intensity intensity);
    virtual void HeadOn(Csis::Type_intensity intensity);
    virtual void SideSwiped(Csis::Type_intensity intensity);
    virtual void TBoned(Csis::Type_intensity intensity);
    virtual void SuspectRollover(Csis::Type_intensity intensity);
    virtual void SuspectAirborne(Csis::Type_intensity intensity);
    virtual void SuspectSpunout(Csis::Type_intensity intensity);
    virtual void SuspectBrake();
    virtual void SwapVoices(EAXCop* cop);
    virtual bool IsPrimary();
    virtual bool IsHeli();
    virtual bool IsCross();
    virtual void SetInFormation(bool yes);
    virtual bool GetInFormation();
    virtual void SetInPosition(bool yes);
    virtual bool GetInPosition();
    virtual void SetTgtOffset(const UMath::Vector3& off);
    virtual const UMath::Vector3 GetTgtOffset();
    virtual bool SetRank(int newrank);
    virtual bool GetRank();
    virtual void JustHitTraffic();
    virtual void WasRammed();
    virtual int GetTimesRammed();
    virtual float GetTimeLastSeen();
    virtual float GetTimeAirborne();
    virtual float GetTimeLastRammed();
    virtual float GetTimeLastClosing();
    virtual float IsAhead();
    virtual void SetAhead(bool ahead);
    virtual void Impact_Suspect_World();
    virtual void Impact_Suspect_Semi();
    virtual void Impact_Suspect_Train();
    virtual void Impact_Suspect_Guardrail();
    virtual void Impact_Suspect_GasStation();
    virtual void Impact_Suspect_Spikebelt();
    virtual void Impact_Suspect_Traffic(Csis::Type_intensity intensity);
};

class EAXDispatch : public EAXCharacter {
  public:
    void BackupReply(EAXCop* cop, int yes, int type);
    void PursuitUpdate(EAXCop* cop);

    void PursuitUpdateHook(EAXCop* cop);
};

class EAXAirSupport : public EAXCop {
  public:
    virtual void JoinRB();
    virtual void IntentToBail();
    virtual void Swarming();
    virtual void HazardAlert(Csis::Type_heli_hazard_alert_type type);
    virtual void Quadrant();
    virtual void QuadrantMoving();

    void Update() override;
    void Bailout() override;
    Csis::Type_heli_bailout_type GetCauseOfBailout();

    void UpdateHook();
    void SetHandleHook(HSIMABLE handle);
    void IntentToRamHook();
    void StrategyResetHook(bool new_strategy);
    void BailoutHook();
    void HazardAlertHook(Csis::Type_heli_hazard_alert_type type);
    void RegainVisualHook();
    void SpottedHook();
    void SwarmingHook();
    void PursuitUpdateReplyHook();
    void SuspectBehaviorHook();
};

class Rain {
  public:
    float GetRainIntensity() {
        return intensity;
    }

    unsigned char mRain[0x28C];
    float         intensity;
};

class SFXCTL_Pathfinder {
  public:
    static int& m_curinteractive;
};

class SoundAI {
  public:
    enum PursuitState {
        kActive      = 0,
        kSearching   = 1,
        kInactive    = 2,
        kOtherTarget = 3,
    };

    static SoundAI* Get();
    static bool     Init(void* module);
    static void     Restore();

    EAXAirSupport* GetHeli() {
        return mHeli;
    }

    EAXDispatch* GetDispatch() {
        return mDispatch;
    }

    PursuitState GetPursuitState() {
        return mPursuitState;
    }

    const int GetHeat() {
        return mPlayerHeat;
    }

    const float GetPlayerSpeed() {
        return mPlayerSpeed;
    }

    const float GetPursuitDuration() {
        return mPursuitDuration;
    }

    EAXCop* GetRandomActiveCop(int type, bool reqLOS);
    void    UpdateStateMachines();
    EAXCop* FindClosestCop(bool enforceLOS, bool includeHeli);
    void    AddNewHeli(IVehicle* heli);

    void UpdateStateMachinesHook();
    void AddNewHeliHook(IVehicle* heli);

    unsigned char  mActivity[0x54];
    unsigned int   mFlags;
    Speech::copMap mActors;
    unsigned char  mUsage[0x70];
    EAXDispatch*   mDispatch;
    EAXCop*        mLeader;
    EAXAirSupport* mHeli;
    unsigned char  mCopsInFormation[0x14];
    float          mDeadAir;
    EAXCop*        mLastCopInFormation;
    EAXCop*        mLatestCop;
    int            mPlayerHeat;
    float          mPlayerSpeed;
    UMath::Vector3 mPlayerPos;
    UMath::Vector3 mPlayerFW;
    UMath::Vector3 mSmoothedFWRoad;
    void*          mPursuit;
    void*          mAIPursuit;
    int            mFocus;
    float          mPursuitDist;
    float          mPursuitDuration;
    unsigned char  mPursuitDetails[0x90];
    PursuitState   mPursuitState;
};
