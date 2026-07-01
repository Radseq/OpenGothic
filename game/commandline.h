#pragma once

#include <Tempest/Platform>
#include <Tempest/Dir>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "game/constants.h"

class VersionInfo;
class GothicNotFoundException : std::logic_error {
  using logic_error::logic_error;
  };

class CommandLine {
  public:
    CommandLine(int argc,const char** argv);
    static const CommandLine& inst();

    enum GraphicBackend : uint8_t {
      Vulkan,
      DirectX12
      };
    auto                graphicsApi() const -> GraphicBackend;
    std::u16string_view rootPath() const;
    std::u16string      scriptPath() const;
    std::u16string      scriptPath(ScriptLang lang) const;
    std::u16string      cutscenePath() const;
    std::u16string      cutscenePath(ScriptLang lang) const;
    std::u16string_view modPath() const { return gmod; }
    std::u16string      nestedPath(const std::initializer_list<const char16_t*> &name, Tempest::Dir::FileType type) const;

    bool                isDevMode()        const { return devmode;      }
    bool                isValidationMode() const { return isDebug;      }
    bool                isWindowMode()     const { return isWindow;     }
    bool                isRayQuery()       const { return isRQuery;     }
    bool                isRtGi()           const { return isGi;         }
    bool                isMeshShading()    const { return isMeshSh;     }
    bool                isBindless()       const { return isBindlessSh; }
    bool                isVirtualShadow()  const { return isVsm;        }
    bool                isSoftwareShadow() const { return isRtSm;       }
    bool                doStartMenu()      const { return !noMenu;      }
    Benchmark           isBenchmarkMode()  const { return isBenchmark;  }
    bool                doForceG1()        const { return forceG1;      }
    bool                doForceG2()        const { return forceG2;      }
    bool                doForceG2NR()      const { return forceG2NR;    }
    bool                aaPreset()         const { return aaPresetId;   }
    std::string_view    defaultSave()      const { return saveDef;      }
    std::string_view    dumpInitialWorld() const { return dumpInitial;  }
    std::string_view    dumpSaveWorld()    const { return dumpSave;     }
    std::string_view    mmoSqlite()        const { return mmoSqliteDb;   }
    uint64_t            mmoSqliteIntervalMs() const { return mmoSqliteInterval; }
    bool                mmoSqliteRestore() const { return mmoSqliteRestoreState; }
    bool                mmoSqliteCaptureBaseline() const { return mmoSqliteCaptureBaselineState; }
    bool                mmoSqliteCapturePreStartExit() const { return mmoSqliteCapturePreStartExitState; }
    std::string_view    mmoActionJsonl() const { return mmoActionJsonlPath; }
    std::string_view    mmoActionUdpEndpoint() const { return mmoActionUdp; }
    bool                mmoClientUsesServer() const { return mmoClientUsesServerState; }
    std::string_view    mmoServerEndpoint() const { return mmoServerEndpointValue; }
    std::string_view    mmoRestoreSnapshotJson() const { return mmoRestoreSnapshotJsonPath; }
    bool                mmoRestoreSnapshotApply() const { return mmoRestoreSnapshotApplyState; }
    std::string_view    mmoServerSnapshotJson() const { return mmoServerSnapshotJsonPath; }
    bool                mmoServerSnapshotApplyInventory() const { return mmoClientUsesServerState; }
    bool                mmoServerSnapshotApplyPosition() const { return mmoClientUsesServerState; }
    bool                mmoServerSnapshotApplyStats() const { return mmoClientUsesServerState; }
    bool                mmoServerSnapshotApplyStory() const { return mmoClientUsesServerState; }
    bool                mmoServerSnapshotApplyWorldState() const { return mmoClientUsesServerState; }
    bool                mmoDbContinueWithoutNativeSave() const { return mmoDbContinueWithoutNativeSaveState; }
    std::string_view    mmoDbContinueSyntheticSlot() const { return "mmo_db_continue.sav"; }
    bool                mmoRequireDbSaveCheckpointRestore() const { return mmoRequireDbSaveCheckpointRestoreState; }
    std::string_view    mmoDbBootstrapWorld() const { return mmoDbBootstrapWorldValue; }
    std::string_view    mmoActionSessionKey() const { return mmoActionSession; }
    uint64_t            mmoActionQueueCapacity() const { return mmoActionQueueCap; }
    bool                mmoActionStrictOverflow() const { return mmoActionStrictOverflowState; }
    uint64_t            mmoActionCheckpointIntervalMs() const { return mmoActionCheckpointInterval; }
    float               mmoActionCheckpointMinDistance() const { return mmoActionCheckpointMinDistanceWorld; }
    float               mmoActionCheckpointMinYawDeg() const { return mmoActionCheckpointMinYaw; }
    uint64_t            mmoActionCheckpointForceIntervalMs() const { return mmoActionCheckpointForceInterval; }
    uint64_t            mmoActionMovementProposalIntervalMs() const { return mmoActionMovementProposalInterval; }
    float               mmoActionMovementProposalMinDistance() const { return mmoActionMovementProposalMinDistanceWorld; }
    float               mmoActionMovementProposalMinYawDeg() const { return mmoActionMovementProposalMinYaw; }

    std::string         wrldDef;

  private:
    bool                validateGothicPath() const;

    GraphicBackend      graphics = GraphicBackend::Vulkan;
    std::u16string      gpath, gmod;
    std::u16string      gscript;
    std::u16string      gcutscene;
    std::string         saveDef;
    std::string         dumpInitial;
    std::string         dumpSave;
    std::string         mmoSqliteDb;
    uint64_t            mmoSqliteInterval = 5000;
    bool                mmoSqliteRestoreState = true;
    bool                mmoSqliteCaptureBaselineState = false;
    bool                mmoSqliteCapturePreStartExitState = false;
    std::string         mmoActionJsonlPath;
    std::string         mmoActionUdp;
    bool                mmoClientUsesServerState = false;
    std::string         mmoServerEndpointValue;
    std::string         mmoRestoreSnapshotJsonPath;
    bool                mmoRestoreSnapshotApplyState = false;
    std::string         mmoServerSnapshotJsonPath = "runtime/mmo_server_bootstrap_snapshot.json";
    bool                mmoDbContinueWithoutNativeSaveState = false;
    bool                mmoRequireDbSaveCheckpointRestoreState = false;
    std::string         mmoDbBootstrapWorldValue;
    std::string         mmoActionSession = "local-dev";
    uint64_t            mmoActionQueueCap = 4096;
    bool                mmoActionStrictOverflowState = false;
    uint64_t            mmoActionCheckpointInterval = 0;
    float               mmoActionCheckpointMinDistanceWorld = 0.f;
    float               mmoActionCheckpointMinYaw = 0.f;
    uint64_t            mmoActionCheckpointForceInterval = 0;
    uint64_t            mmoActionMovementProposalInterval = 0;
    float               mmoActionMovementProposalMinDistanceWorld = 0.f;
    float               mmoActionMovementProposalMinYaw = 0.f;
    bool                devmode      = false;
    bool                noMenu       = false;
    Benchmark           isBenchmark  = Benchmark::None;
    bool                isWindow     = false;
    bool                isDebug      = false;
#if defined(__OSX__)
    bool                isRQuery     = false;
    bool                isMeshSh     = false;
#else
    bool                isRQuery     = true;
    bool                isMeshSh     = true;
#endif
    bool                isBindlessSh = true;
    bool                isVsm        = false;
    bool                isRtSm       = false;
    bool                isGi         = false;
    bool                forceG1      = false;
    bool                forceG2      = false;
    bool                forceG2NR    = false;
    uint32_t            aaPresetId = 0;
  };
