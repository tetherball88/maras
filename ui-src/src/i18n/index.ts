import { useCallback } from 'react'
import { BASE_EN_STRINGS, useLocaleStore } from '../store/useLocaleStore'

export type TranslationVars = Record<string, string | number | boolean | undefined | null>

const pluralRulesCache = new Map<string, Intl.PluralRules>()

function getPluralRules(langCode: string): Intl.PluralRules {
  const normalized = (langCode || 'en').replace('_', '-')
  const cached = pluralRulesCache.get(normalized)
  if (cached) return cached
  try {
    const rules = new Intl.PluralRules(normalized)
    pluralRulesCache.set(normalized, rules)
    return rules
  } catch {
    const fallback = new Intl.PluralRules('en')
    pluralRulesCache.set(normalized, fallback)
    return fallback
  }
}

/**
 * Crash-safe `{named}` template interpolation matching C++ `FormatTemplate`.
 * - `{{` and `}}` render as literal `{` and `}`.
 * - Unknown `{tokens}` or malformed braces remain verbatim without throwing.
 */
export function interpolateTemplate(tmpl: string, vars?: TranslationVars): string {
  if (!tmpl || !vars) {
    if (!tmpl) return ''
    return tmpl.replace(/\{\{/g, '{').replace(/\}\}/g, '}')
  }

  let out = ''
  const len = tmpl.length
  let i = 0
  while (i < len) {
    const ch = tmpl[i]
    if (ch === '{') {
      if (i + 1 < len && tmpl[i + 1] === '{') {
        out += '{'
        i += 2
        continue
      }
      const closePos = tmpl.indexOf('}', i + 1)
      if (closePos > i + 1) {
        const token = tmpl.slice(i + 1, closePos)
        if (/^[A-Za-z0-9_.-]+$/.test(token)) {
          const val = vars[token]
          if (val !== undefined && val !== null) {
            out += String(val)
          } else {
            out += tmpl.slice(i, closePos + 1)
          }
          i = closePos + 1
          continue
        }
      }
      out += '{'
      i += 1
      continue
    }
    if (ch === '}' && i + 1 < len && tmpl[i + 1] === '}') {
      out += '}'
      i += 2
      continue
    }
    out += ch
    i += 1
  }
  return out
}

/**
 * Imperative translation lookup (safe in utilities, Zustand actions, and class components).
 */
export function t(key: string, vars?: TranslationVars, fallback?: string): string {
  const dict = useLocaleStore.getState().dict
  const raw = dict[key] ?? BASE_EN_STRINGS[key] ?? fallback ?? key
  return interpolateTemplate(raw, vars)
}

/**
 * Imperative CLDR plural translation lookup (`<keyStem>_one`, `_few`, `_many`, `_other`).
 * Automatically binds `{count}` unless already provided in `vars`.
 */
export function tp(
  keyStem: string,
  count: number,
  vars?: TranslationVars,
  fallbackOther?: string,
): string {
  const { dict, resolvedLanguage } = useLocaleStore.getState()
  const category = getPluralRules(resolvedLanguage).select(count)
  const catKey = `${keyStem}_${category}`
  const otherKey = `${keyStem}_other`

  const raw =
    dict[catKey] ??
    dict[otherKey] ??
    dict[keyStem] ??
    BASE_EN_STRINGS[catKey] ??
    BASE_EN_STRINGS[otherKey] ??
    BASE_EN_STRINGS[keyStem] ??
    fallbackOther ??
    keyStem

  const mergedVars: TranslationVars =
    vars && vars.count !== undefined ? vars : { ...(vars || {}), count }
  return interpolateTemplate(raw, mergedVars)
}

/**
 * Reactive React translation hook — re-renders automatically when language or live dictionary updates.
 */
export function useTranslation() {
  const revision = useLocaleStore((s) => s.revision)
  const resolvedLanguage = useLocaleStore((s) => s.resolvedLanguage)
  const selectedLanguage = useLocaleStore((s) => s.selectedLanguage)
  const detectedSkyrimLanguage = useLocaleStore((s) => s.detectedSkyrimLanguage)
  const availableLanguages = useLocaleStore((s) => s.availableLanguages)

  const translate = useCallback(
    (key: string, vars?: TranslationVars, fallback?: string) => {
      void revision
      return t(key, vars, fallback)
    },
    [revision],
  )

  const translatePlural = useCallback(
    (keyStem: string, count: number, vars?: TranslationVars, fallbackOther?: string) => {
      void revision
      return tp(keyStem, count, vars, fallbackOther)
    },
    [revision],
  )

  return {
    t: translate,
    tp: translatePlural,
    lang: resolvedLanguage,
    selectedLanguage,
    detectedSkyrimLanguage,
    availableLanguages,
    revision,
  }
}
