import { RelationshipPair, StatusChangeRecord } from '../types'

export function getLatestStatusRecord(pair?: RelationshipPair | null): StatusChangeRecord | undefined {
  if (!pair || !pair.statusHistory || pair.statusHistory.length === 0) return undefined
  return pair.statusHistory[pair.statusHistory.length - 1]
}

export function formatGameDay(day?: number, currentGameDay?: number): string {
  if (day === undefined || day === null) return ''
  if (day < 0) return 'Historical'

  if (currentGameDay != null && currentGameDay >= 0) {
    const diff = Math.floor(currentGameDay - day)
    if (diff <= 0) return 'Today'
    if (diff === 1) return '1 day ago'
    if (diff < 7) return `${diff} days ago`

    if (diff < 30) {
      const weeks = Math.floor(diff / 7)
      return weeks === 1 ? '1 week ago' : `${weeks} weeks ago`
    }

    if (diff < 365) {
      const months = Math.floor(diff / 30)
      return months === 1 ? '1 month ago' : `${months} months ago`
    }

    const years = Math.floor(diff / 365)
    return years === 1 ? '1 year ago' : `${years} years ago`
  }

  return `Day ${Math.floor(day)}`
}

export function formatRelationshipDate(date?: number, prefix: string = 'Married', currentGameDay?: number): string {
  if (date === undefined || date === null) return ''
  if (date < 0) return `${prefix} (Historical)`

  const formatted = formatGameDay(date, currentGameDay)
  if (formatted === 'Today') return `${prefix} Today`
  return `${prefix} ${formatted}`
}
