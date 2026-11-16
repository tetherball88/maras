import { create } from 'zustand'
import enDashboard from '@locales-en/ui_dashboard.json'
import enSettings from '@locales-en/ui_settings.json'
import { LocaleMeta, LocalePayload } from '../types'

function flattenStrings(
  node: Record<string, unknown>,
  prefix = '',
  out: Record<string, string> = {},
): Record<string, string> {
  for (const [key, val] of Object.entries(node || {})) {
    if (!key || key.startsWith('$comment') || key.startsWith('_comment')) continue
    const fullKey = prefix ? `${prefix}.${key}` : key
    if (typeof val === 'string') {
      out[fullKey] = val
    } else if (val && typeof val === 'object' && !Array.isArray(val)) {
      flattenStrings(val as Record<string, unknown>, fullKey, out)
    }
  }
  return out
}

export const BASE_EN_STRINGS: Readonly<Record<string, string>> = Object.freeze({
  ...flattenStrings(enDashboard as Record<string, unknown>),
  ...flattenStrings(enSettings as Record<string, unknown>),
})

const DEFAULT_EN_META: LocaleMeta = {
  code: 'en',
  name: 'English',
  nativeName: 'English',
}

function applyDocumentFontOverride(fontFamily?: string, resolvedLang?: string) {
  if (typeof document === 'undefined') return
  const root = document.documentElement
  if (resolvedLang) {
    root.lang = resolvedLang
  }
  const trimmed = (fontFamily || '').trim()
  if (trimmed) {
    root.style.setProperty('--font-ui', trimmed)
  } else {
    root.style.removeProperty('--font-ui')
  }
}

export interface LocaleStoreState {
  selectedLanguage: string
  resolvedLanguage: string
  detectedSkyrimLanguage: string
  fontFamily: string
  availableLanguages: LocaleMeta[]
  dict: Record<string, string>
  revision: number
  applyLocalePayload: (payload: Partial<LocalePayload>) => void
}

export const useLocaleStore = create<LocaleStoreState>((set) => ({
  selectedLanguage: 'auto',
  resolvedLanguage: 'en',
  detectedSkyrimLanguage: 'en',
  fontFamily: '',
  availableLanguages: [DEFAULT_EN_META],
  dict: { ...BASE_EN_STRINGS },
  revision: 0,
  applyLocalePayload: (payload) => {
    set((state) => {
      const nextResolved = (payload.resolvedLanguage || state.resolvedLanguage || 'en').trim() || 'en'
      const nextSelected = (payload.selectedLanguage || state.selectedLanguage || 'auto').trim() || 'auto'
      const nextDetected = (payload.detectedSkyrimLanguage || state.detectedSkyrimLanguage || 'en').trim() || 'en'
      const nextFont = typeof payload.fontFamily === 'string' ? payload.fontFamily : state.fontFamily
      const nextLangs =
        Array.isArray(payload.availableLanguages) && payload.availableLanguages.length > 0
          ? payload.availableLanguages
          : state.availableLanguages
      const nextDict: Record<string, string> = {
        ...BASE_EN_STRINGS,
        ...(payload.strings && typeof payload.strings === 'object' ? payload.strings : {}),
      }

      applyDocumentFontOverride(nextFont, nextResolved)

      return {
        selectedLanguage: nextSelected,
        resolvedLanguage: nextResolved,
        detectedSkyrimLanguage: nextDetected,
        fontFamily: nextFont,
        availableLanguages: nextLangs,
        dict: nextDict,
        revision: state.revision + 1,
      }
    })
  },
}))
