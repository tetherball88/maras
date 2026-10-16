import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import { Wrench, Database, Download, CheckCircle2, AlertCircle, Loader2, Users } from 'lucide-react'

export const CheatsSettings: React.FC = () => {
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const dumpData = useMarasStore((s) => s.dumpData)
  const isDumpingData = useMarasStore((s) => s.isDumpingData)
  const lastDumpResult = useMarasStore((s) => s.lastDumpResult)

  const dumpNPCIndex = useMarasStore((s) => s.dumpNPCIndex)
  const isDumpingNPCIndex = useMarasStore((s) => s.isDumpingNPCIndex)
  const lastNPCIndexDumpResult = useMarasStore((s) => s.lastNPCIndexDumpResult)

  const logLevelOptions = [
    { value: 0, label: 'Trace' },
    { value: 1, label: 'Debug' },
    { value: 2, label: 'Info' },
    { value: 3, label: 'Warning' },
    { value: 4, label: 'Error' },
    { value: 5, label: 'None' },
  ]

  const logDestOptions = [
    { value: 0, label: 'Log File' },
    { value: 1, label: 'Console' },
    { value: 2, label: 'Floating Screen' },
  ]

  return (
    <div className="space-y-[1rem]">
      <SectionCard title="Cheats, Debug & Logging" icon={Wrench}>
        <SettingRow
          label="Cheat: Always Success Marriage"
          description="Bypass marriage difficulty calculations and guarantee 100% proposal acceptance."
        >
          <ToggleSwitch
            checked={settings.alwaysSuccess}
            onChange={(val) => updateSetting('alwaysSuccess', val)}
          />
        </SettingRow>

        <SettingRow
          label="Cheat: Add Debug Inspection Spell"
          description="Grant player the M.A.R.A.S Debug Spell to inspect NPC stats in-game."
        >
          <ToggleSwitch
            checked={settings.debugSpell}
            onChange={(val) => updateSetting('debugSpell', val)}
          />
        </SettingRow>

        <SettingRow
          label="Debug: Simulate Non-SkyrimNet Environment"
          description="Force M.A.R.A.S to simulate SkyrimNet being absent. Disables AI dialogue events, prompts, and LLM matchmaking, enabling traditional non-AI fallbacks without disabling SkyrimNet.esp/dll."
        >
          <ToggleSwitch
            checked={Boolean(settings.simulateNoSkyrimNet)}
            onChange={(val) => updateSetting('simulateNoSkyrimNet', val)}
          />
        </SettingRow>

        <SettingRow
          label="SKSE Logging Verbosity Level"
          description="Configure spdlog output threshold for C++ plugin logs."
        >
          <SelectInput
            value={settings.logLevel}
            options={logLevelOptions}
            onChange={(val) => updateSetting('logLevel', parseInt(val))}
          />
        </SettingRow>

        <SettingRow
          label="Log Output Destination"
          description="Select where plugin log messages are written."
        >
          <SelectInput
            value={settings.logDestination}
            options={logDestOptions}
            onChange={(val) => updateSetting('logDestination', parseInt(val))}
          />
        </SettingRow>
      </SectionCard>

      <SectionCard title="Data Export & Diagnostics" icon={Database}>
        <SettingRow
          label="Dump All Stored Data to JSON"
          description="Export all MARAS relationship pairs, NPC profiles, affection values, child registry, household tenancy, spouse hierarchy, and settings into a timestamped JSON file (Data/SKSE/Plugins/MARAS/dumps/)."
        >
          <button
            type="button"
            onClick={() => dumpData()}
            disabled={isDumpingData}
            className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-accent-gold/15 hover:bg-accent-gold/25 text-accent-gold font-medium text-xs border border-accent-gold/40 transition-all cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed shadow-sm active:scale-95"
          >
            {isDumpingData ? (
              <>
                <Loader2 className="w-3.5 h-3.5 animate-spin" />
                <span>Dumping Data...</span>
              </>
            ) : (
              <>
                <Download className="w-3.5 h-3.5" />
                <span>Dump Data to JSON</span>
              </>
            )}
          </button>
        </SettingRow>

        {lastDumpResult && (
          <div
            className={`mx-3 mb-3 p-3 rounded-md text-xs border ${lastDumpResult.success
                ? 'bg-status-married/10 border-status-married/30 text-foreground-primary'
                : 'bg-red-500/10 border-red-500/30 text-red-400'
              }`}
          >
            <div className="flex items-center gap-2 font-semibold">
              {lastDumpResult.success ? (
                <>
                  <CheckCircle2 className="w-4 h-4 text-status-married flex-shrink-0" />
                  <span className="text-status-married">Data Dump Successful</span>
                </>
              ) : (
                <>
                  <AlertCircle className="w-4 h-4 text-red-400 flex-shrink-0" />
                  <span className="text-red-400">Data Dump Failed</span>
                </>
              )}
              <span className="text-[0.6875rem] text-foreground-muted ml-auto font-normal">
                {lastDumpResult.timestamp}
              </span>
            </div>
            {lastDumpResult.success ? (
              <div className="mt-2 space-y-1 text-[0.6875rem] text-foreground-secondary">
                <div>
                  <span className="text-foreground-muted">Saved File: </span>
                  <span className="font-mono text-accent-gold select-all font-medium">
                    {lastDumpResult.fileName || lastDumpResult.filePath}
                  </span>
                </div>
                {lastDumpResult.absolutePath && (
                  <div className="truncate" title={lastDumpResult.absolutePath}>
                    <span className="text-foreground-muted">Full Path: </span>
                    <span className="font-mono text-foreground-muted select-all">
                      {lastDumpResult.absolutePath}
                    </span>
                  </div>
                )}
              </div>
            ) : (
              <div className="mt-1 text-[0.6875rem] text-red-300">
                {lastDumpResult.error || 'An unknown error occurred while saving dump file.'}
              </div>
            )}
          </div>
        )}

        <div className="border-t border-border-subtle/50 my-2" />

        <SettingRow
          label="Dump NPC Index Results"
          description="Export all indexed NPCs (Base & Ref FormIDs, Names, EditorIDs, Races, Sex, Child status, loaded state, and INI rules) currently cached in the M.A.R.A.S NPC Index to a timestamped file (Data/SKSE/Plugins/MARAS/dumps/)."
        >
          <button
            type="button"
            onClick={() => dumpNPCIndex()}
            disabled={isDumpingNPCIndex}
            className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-accent-gold/15 hover:bg-accent-gold/25 text-accent-gold font-medium text-xs border border-accent-gold/40 transition-all cursor-pointer disabled:opacity-50 disabled:cursor-not-allowed shadow-sm active:scale-95"
          >
            {isDumpingNPCIndex ? (
              <>
                <Loader2 className="w-3.5 h-3.5 animate-spin" />
                <span>Dumping Index...</span>
              </>
            ) : (
              <>
                <Users className="w-3.5 h-3.5" />
                <span>Dump NPC Index</span>
              </>
            )}
          </button>
        </SettingRow>

        {lastNPCIndexDumpResult && (
          <div
            className={`mx-3 mb-3 p-3 rounded-md text-xs border ${lastNPCIndexDumpResult.success
                ? 'bg-status-married/10 border-status-married/30 text-foreground-primary'
                : 'bg-red-500/10 border-red-500/30 text-red-400'
              }`}
          >
            <div className="flex items-center gap-2 font-semibold">
              {lastNPCIndexDumpResult.success ? (
                <>
                  <CheckCircle2 className="w-4 h-4 text-status-married flex-shrink-0" />
                  <span className="text-status-married">NPC Index Dump Successful</span>
                </>
              ) : (
                <>
                  <AlertCircle className="w-4 h-4 text-red-400 flex-shrink-0" />
                  <span className="text-red-400">NPC Index Dump Failed</span>
                </>
              )}
              <span className="text-[0.6875rem] text-foreground-muted ml-auto font-normal">
                {lastNPCIndexDumpResult.timestamp}
              </span>
            </div>
            {lastNPCIndexDumpResult.success ? (
              <div className="mt-2 space-y-1 text-[0.6875rem] text-foreground-secondary">
                <div>
                  <span className="text-foreground-muted">Saved File: </span>
                  <span className="font-mono text-accent-gold select-all font-medium">
                    {lastNPCIndexDumpResult.fileName || lastNPCIndexDumpResult.filePath}
                  </span>
                </div>
                {lastNPCIndexDumpResult.totalNPCs !== undefined && (
                  <div>
                    <span className="text-foreground-muted">Total Indexed NPCs: </span>
                    <span className="font-mono text-foreground-primary font-medium">
                      {lastNPCIndexDumpResult.totalNPCs.toLocaleString()}
                    </span>
                  </div>
                )}
                {lastNPCIndexDumpResult.absolutePath && (
                  <div className="truncate" title={lastNPCIndexDumpResult.absolutePath}>
                    <span className="text-foreground-muted">Full Path: </span>
                    <span className="font-mono text-foreground-muted select-all">
                      {lastNPCIndexDumpResult.absolutePath}
                    </span>
                  </div>
                )}
              </div>
            ) : (
              <div className="mt-1 text-[0.6875rem] text-red-300">
                {lastNPCIndexDumpResult.error || 'An unknown error occurred while saving NPC index dump.'}
              </div>
            )}
          </div>
        )}
      </SectionCard>
    </div>
  )
}

