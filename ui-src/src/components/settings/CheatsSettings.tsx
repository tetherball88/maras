import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { useTranslation } from '../../i18n'
import { SettingRow } from './SettingRow'
import { ToggleSwitch } from '../shared/ToggleSwitch'
import { SelectInput } from '../shared/SelectInput'
import { SectionCard } from '../shared/SectionCard'
import {
  Wrench,
  Database,
  Download,
  CheckCircle2,
  AlertCircle,
  Loader2,
  Users,
} from 'lucide-react'

export const CheatsSettings: React.FC = () => {
  const { t, tp } = useTranslation()
  void tp
  const settings = useMarasStore((s) => s.settings)
  const updateSetting = useMarasStore((s) => s.updateSetting)
  const dumpData = useMarasStore((s) => s.dumpData)
  const isDumpingData = useMarasStore((s) => s.isDumpingData)
  const lastDumpResult = useMarasStore((s) => s.lastDumpResult)

  const dumpNPCIndex = useMarasStore((s) => s.dumpNPCIndex)
  const isDumpingNPCIndex = useMarasStore((s) => s.isDumpingNPCIndex)
  const lastNPCIndexDumpResult = useMarasStore((s) => s.lastNPCIndexDumpResult)

  const logLevelOptions = [
    { value: 0, label: t('settings.cheats.logLevelTrace') },
    { value: 1, label: t('settings.cheats.logLevelDebug') },
    { value: 2, label: t('settings.cheats.logLevelInfo') },
    { value: 3, label: t('settings.cheats.logLevelWarning') },
    { value: 4, label: t('settings.cheats.logLevelError') },
    { value: 5, label: t('settings.cheats.logLevelNone') },
  ]

  const logDestOptions = [
    { value: 0, label: t('settings.cheats.logDestFile') },
    { value: 1, label: t('settings.cheats.logDestConsole') },
    { value: 2, label: t('settings.cheats.logDestScreen') },
  ]

  return (
    <div className="space-y-[1rem]">
      <SectionCard title={t('settings.cheats.debugTitle')} icon={Wrench}>
        <SettingRow
          label={t('settings.cheats.alwaysSuccessLabel')}
          description={t('settings.cheats.alwaysSuccessDesc')}
        >
          <ToggleSwitch
            checked={settings.alwaysSuccess}
            onChange={(val) => updateSetting('alwaysSuccess', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.cheats.debugSpellLabel')}
          description={t('settings.cheats.debugSpellDesc')}
        >
          <ToggleSwitch
            checked={settings.debugSpell}
            onChange={(val) => updateSetting('debugSpell', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.cheats.simulateNoSkyrimNetLabel')}
          description={t('settings.cheats.simulateNoSkyrimNetDesc')}
        >
          <ToggleSwitch
            checked={Boolean(settings.simulateNoSkyrimNet)}
            onChange={(val) => updateSetting('simulateNoSkyrimNet', val)}
          />
        </SettingRow>

        <SettingRow
          label={t('settings.cheats.logLevelLabel')}
          description={t('settings.cheats.logLevelDesc')}
        >
          <SelectInput
            value={settings.logLevel}
            options={logLevelOptions}
            onChange={(val) => updateSetting('logLevel', parseInt(val))}
            className="min-w-[10rem] w-auto"
          />
        </SettingRow>

        <SettingRow
          label={t('settings.cheats.logDestLabel')}
          description={t('settings.cheats.logDestDesc')}
        >
          <SelectInput
            value={settings.logDestination}
            options={logDestOptions}
            onChange={(val) => updateSetting('logDestination', parseInt(val))}
            className="min-w-[11rem] w-auto"
          />
        </SettingRow>
      </SectionCard>

      <SectionCard title={t('settings.cheats.exportTitle')} icon={Database}>
        <SettingRow
          label={t('settings.cheats.dumpDataLabel')}
          description={t('settings.cheats.dumpDataDesc')}
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
                <span>{t('settings.cheats.dumpingDataBtn')}</span>
              </>
            ) : (
              <>
                <Download className="w-3.5 h-3.5" />
                <span>{t('settings.cheats.dumpDataBtn')}</span>
              </>
            )}
          </button>
        </SettingRow>

        {lastDumpResult && (
          <div
            className={`mx-3 mb-3 p-3 rounded-md text-xs border ${
              lastDumpResult.success
                ? 'bg-status-married/10 border-status-married/30 text-foreground-primary'
                : 'bg-red-500/10 border-red-500/30 text-red-400'
            }`}
          >
            <div className="flex items-center gap-2 font-semibold">
              {lastDumpResult.success ? (
                <>
                  <CheckCircle2 className="w-4 h-4 text-status-married flex-shrink-0" />
                  <span className="text-status-married">
                    {t('settings.cheats.dumpDataSuccess')}
                  </span>
                </>
              ) : (
                <>
                  <AlertCircle className="w-4 h-4 text-red-400 flex-shrink-0" />
                  <span className="text-red-400">{t('settings.cheats.dumpDataFailed')}</span>
                </>
              )}
              <span className="text-[0.6875rem] text-foreground-muted ml-auto font-normal">
                {lastDumpResult.timestamp}
              </span>
            </div>
            {lastDumpResult.success ? (
              <div className="mt-2 space-y-1 text-[0.6875rem] text-foreground-secondary">
                <div>
                  <span className="text-foreground-muted">
                    {t('settings.cheats.savedFilePrefix')}{' '}
                  </span>
                  <span className="font-mono text-accent-gold select-all font-medium">
                    {lastDumpResult.fileName || lastDumpResult.filePath}
                  </span>
                </div>
                {lastDumpResult.absolutePath && (
                  <div className="truncate" title={lastDumpResult.absolutePath}>
                    <span className="text-foreground-muted">
                      {t('settings.cheats.fullPathPrefix')}{' '}
                    </span>
                    <span className="font-mono text-foreground-muted select-all">
                      {lastDumpResult.absolutePath}
                    </span>
                  </div>
                )}
              </div>
            ) : (
              <div className="mt-1 text-[0.6875rem] text-red-300">
                {lastDumpResult.error || t('settings.cheats.dumpDataUnknownError')}
              </div>
            )}
          </div>
        )}

        <div className="border-t border-border-subtle/50 my-2" />

        <SettingRow
          label={t('settings.cheats.dumpNpcIndexLabel')}
          description={t('settings.cheats.dumpNpcIndexDesc')}
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
                <span>{t('settings.cheats.dumpingNpcIndexBtn')}</span>
              </>
            ) : (
              <>
                <Users className="w-3.5 h-3.5" />
                <span>{t('settings.cheats.dumpNpcIndexBtn')}</span>
              </>
            )}
          </button>
        </SettingRow>

        {lastNPCIndexDumpResult && (
          <div
            className={`mx-3 mb-3 p-3 rounded-md text-xs border ${
              lastNPCIndexDumpResult.success
                ? 'bg-status-married/10 border-status-married/30 text-foreground-primary'
                : 'bg-red-500/10 border-red-500/30 text-red-400'
            }`}
          >
            <div className="flex items-center gap-2 font-semibold">
              {lastNPCIndexDumpResult.success ? (
                <>
                  <CheckCircle2 className="w-4 h-4 text-status-married flex-shrink-0" />
                  <span className="text-status-married">
                    {t('settings.cheats.dumpNpcIndexSuccess')}
                  </span>
                </>
              ) : (
                <>
                  <AlertCircle className="w-4 h-4 text-red-400 flex-shrink-0" />
                  <span className="text-red-400">{t('settings.cheats.dumpNpcIndexFailed')}</span>
                </>
              )}
              <span className="text-[0.6875rem] text-foreground-muted ml-auto font-normal">
                {lastNPCIndexDumpResult.timestamp}
              </span>
            </div>
            {lastNPCIndexDumpResult.success ? (
              <div className="mt-2 space-y-1 text-[0.6875rem] text-foreground-secondary">
                <div>
                  <span className="text-foreground-muted">
                    {t('settings.cheats.savedFilePrefix')}{' '}
                  </span>
                  <span className="font-mono text-accent-gold select-all font-medium">
                    {lastNPCIndexDumpResult.fileName || lastNPCIndexDumpResult.filePath}
                  </span>
                </div>
                {lastNPCIndexDumpResult.totalNPCs !== undefined && (
                  <div>
                    <span className="text-foreground-muted">
                      {t('settings.cheats.totalIndexedNpcsPrefix')}{' '}
                    </span>
                    <span className="font-mono text-foreground-primary font-medium">
                      {lastNPCIndexDumpResult.totalNPCs.toLocaleString()}
                    </span>
                  </div>
                )}
                {lastNPCIndexDumpResult.absolutePath && (
                  <div className="truncate" title={lastNPCIndexDumpResult.absolutePath}>
                    <span className="text-foreground-muted">
                      {t('settings.cheats.fullPathPrefix')}{' '}
                    </span>
                    <span className="font-mono text-foreground-muted select-all">
                      {lastNPCIndexDumpResult.absolutePath}
                    </span>
                  </div>
                )}
              </div>
            ) : (
              <div className="mt-1 text-[0.6875rem] text-red-300">
                {lastNPCIndexDumpResult.error || t('settings.cheats.dumpNpcIndexUnknownError')}
              </div>
            )}
          </div>
        )}
      </SectionCard>
    </div>
  )
}
