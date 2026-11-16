import { t, tp } from '../i18n'
import { RelationshipPair, StatusChangeRecord } from '../types'

export function getLatestStatusRecord(pair?: RelationshipPair | null): StatusChangeRecord | undefined {
  if (!pair || !pair.statusHistory || pair.statusHistory.length === 0) return undefined
  return pair.statusHistory[pair.statusHistory.length - 1]
}

export function formatGameDay(gameDay?: number, currentGameDay?: number): string {
  if (gameDay === undefined || gameDay === null) return ''
  if (Number.isNaN(gameDay)) return t('utils.date.unknown')
  if (gameDay < 0) return t('utils.date.historical')

  if (currentGameDay != null && currentGameDay >= 0) {
    const days = Math.floor(currentGameDay - gameDay)
    if (days <= 0) return t('utils.date.today')
    if (days < 7) return tp('utils.date.daysAgo', days)

    if (days < 30) {
      const weeks = Math.floor(days / 7)
      return tp('utils.date.weeksAgo', weeks)
    }

    if (days < 365) {
      const months = Math.floor(days / 30)
      return tp('utils.date.monthsAgo', months)
    }

    const years = Math.floor(days / 365)
    return tp('utils.date.yearsAgo', years)
  }

  return t('utils.date.dayNumber', { day: Math.floor(gameDay) })
}

export function formatRelationshipDate(date?: number, prefix?: string, currentGameDay?: number): string {
  if (date === undefined || date === null) return ''
  const resolvedPrefix = prefix ?? t('utils.date.marriedPrefix')
  if (date < 0) return t('utils.date.withPrefixHistorical', { prefix: resolvedPrefix })

  const formatted = formatGameDay(date, currentGameDay)
  return t('utils.date.withPrefix', { prefix: resolvedPrefix, date: formatted })
}
