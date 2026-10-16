import React, { useState, useEffect, useMemo } from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { BuffCard } from './BuffCard'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Switch } from '@/components/ui/switch'
import { Input } from '@/components/ui/input'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import {
  Sparkles,
  Shield,
  Swords,
  Search,
  RotateCw,
  EyeOff,
  Users,
  Info,
} from 'lucide-react'

export const HouseholdBuffsPage: React.FC = () => {
  const householdBuffs = useMarasStore((s) => s.householdBuffs)
  const isLoadingBuffs = useMarasStore((s) => s.isLoadingBuffs)
  const fetchHouseholdBuffs = useMarasStore((s) => s.fetchHouseholdBuffs)
  const openNpcDetail = useMarasStore((s) => s.openNpcDetail)

  const [searchQuery, setSearchQuery] = useState('')
  const [hideInactive, setHideInactive] = useState(false)

  // Fetch live buffs on page mount
  useEffect(() => {
    fetchHouseholdBuffs()
  }, [fetchHouseholdBuffs])

  const permanentBuffs = householdBuffs?.permanentBuffs || []
  const followerBuffs = householdBuffs?.followerBuffs || []

  // Filter and sort buffs: Active buffs first, then alphabetical
  const filterAndSort = (buffs: typeof permanentBuffs) => {
    return buffs
      .filter((b) => {
        if (hideInactive && !b.isActive) return false
        if (!searchQuery.trim()) return true

        const q = searchQuery.toLowerCase().trim()
        const matchesName = b.name.toLowerCase().includes(q)
        const matchesCategory = b.category.toLowerCase().includes(q)
        const matchesDesc = b.description.toLowerCase().includes(q)
        const matchesContributor = b.contributors.some((c) => c.name.toLowerCase().includes(q))
        const matchesPotential = b.potentialSpouses?.some((p) => p.name.toLowerCase().includes(q))

        return matchesName || matchesCategory || matchesDesc || matchesContributor || matchesPotential
      })
      .sort((a, b) => {
        if (a.isActive !== b.isActive) {
          return a.isActive ? -1 : 1
        }
        return a.name.localeCompare(b.name)
      })
  }

  const filteredPermanent = useMemo(
    () => filterAndSort(permanentBuffs),
    [permanentBuffs, searchQuery, hideInactive]
  )

  const filteredFollower = useMemo(
    () => filterAndSort(followerBuffs),
    [followerBuffs, searchQuery, hideInactive]
  )

  const activePermCount = householdBuffs?.activePermanentCount ?? permanentBuffs.filter((b) => b.isActive).length
  const activeFollCount = householdBuffs?.activeFollowerCount ?? followerBuffs.filter((b) => b.isActive).length

  return (
    <div className="flex flex-col h-full w-full gap-[0.875rem] overflow-hidden">
      {/* Header Banner */}
      <div className="bg-panel-raised border border-border-divider p-[1rem] rounded-lg flex flex-col md:flex-row md:items-center justify-between gap-[0.75rem] shadow-sm">
        <div>
          <div className="flex items-center gap-[0.5rem]">
            <Sparkles className="w-[1.25em] h-[1.25em] text-accent-bright" />
            <h1 className="text-lg font-bold text-foreground-primary tracking-wide">
              Household Buffs Summary
            </h1>
            <Badge variant="default" className="text-xs">
              {activePermCount + activeFollCount} Active
            </Badge>
          </div>
          <p className="text-xs text-foreground-secondary mt-[0.25rem]">
            Bonuses granted by your spouses: permanent household perks scaled by hierarchy rank and affection, plus combat bonuses when travelling together.
          </p>
        </div>

        {/* Quick Stats & Refresh */}
        <div className="flex items-center gap-[0.75rem] flex-wrap">
          <div className="flex items-center gap-[0.5rem] bg-panel-inset px-[0.625rem] py-[0.375rem] rounded border border-border-subtle">
            <Shield className="w-[0.9em] h-[0.9em] text-accent" />
            <span className="text-xs text-foreground-muted">Permanent:</span>
            <span className="text-xs font-bold font-mono text-accent-bright">
              {activePermCount} / {permanentBuffs.length}
            </span>
          </div>

          <div className="flex items-center gap-[0.5rem] bg-panel-inset px-[0.625rem] py-[0.375rem] rounded border border-border-subtle">
            <Swords className="w-[0.9em] h-[0.9em] text-accent" />
            <span className="text-xs text-foreground-muted">Follower:</span>
            <span className="text-xs font-bold font-mono text-accent-bright">
              {activeFollCount} / {followerBuffs.length}
            </span>
          </div>

          <Button
            variant="outline"
            size="sm"
            onClick={() => fetchHouseholdBuffs()}
            disabled={isLoadingBuffs}
            className="flex items-center gap-[0.375rem] text-xs h-[2rem]"
          >
            <RotateCw className={`w-[0.9em] h-[0.9em] ${isLoadingBuffs ? 'animate-spin text-accent' : ''}`} />
            <span>Refresh</span>
          </Button>
        </div>
      </div>

      {/* Filter and Search Bar */}
      <div className="flex items-center justify-between gap-[0.75rem] flex-wrap bg-panel p-[0.625rem] px-[0.875rem] rounded-md border border-border-subtle">
        <div className="relative flex-1 min-w-[14rem] max-w-[22rem]">
          <Search className="absolute left-[0.625rem] top-1/2 -translate-y-1/2 w-[0.9em] h-[0.9em] text-foreground-muted" />
          <Input
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Search by buff, class, or spouse..."
            className="pl-[2rem] h-[2rem] text-xs bg-panel-inset border-border-subtle"
          />
        </div>

        <div className="flex items-center gap-[0.75rem]">
          <label className="flex items-center gap-[0.5rem] text-xs text-foreground-secondary cursor-pointer select-none">
            <EyeOff className="w-[0.9em] h-[0.9em] text-foreground-muted" />
            <span>Hide Inactive Buffs</span>
            <Switch
              checked={hideInactive}
              onCheckedChange={setHideInactive}
              className="scale-90"
            />
          </label>
        </div>
      </div>

      {/* Main Scrollable Content */}
      <ScrollArea className="flex-1 min-h-0 h-full pr-[0.5rem]">
        <div className="space-y-[1.25rem] pb-[1.5rem]">
          {/* Section 1: Permanent Buffs */}
          <section className="space-y-[0.625rem]">
            <div className="flex items-center justify-between border-b border-border-divider pb-[0.375rem]">
              <div className="flex items-center gap-[0.5rem]">
                <Shield className="w-[1em] h-[1em] text-accent" />
                <h2 className="text-sm font-bold text-foreground-primary uppercase tracking-wider">
                  Permanent Household Buffs
                </h2>
                <span className="text-xs text-foreground-muted">
                  ({filteredPermanent.length})
                </span>
              </div>

              <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem]">
                <Info className="w-[0.9em] h-[0.9em]" />
                <span>Active 24/7 from married spouses based on Social Class</span>
              </div>
            </div>

            {filteredPermanent.length > 0 ? (
              <div className="grid grid-cols-1 lg:grid-cols-2 gap-[0.625rem] items-stretch">
                {filteredPermanent.map((buff) => (
                  <BuffCard
                    key={buff.id}
                    buff={buff}
                    onNpcClick={openNpcDetail}
                  />
                ))}
              </div>
            ) : (
              <div className="p-[1.25rem] text-center bg-panel-inset/50 rounded border border-border-divider text-xs text-foreground-muted">
                No permanent buffs match the current filter.
              </div>
            )}
          </section>

          {/* Section 2: Follower Buffs */}
          <section className="space-y-[0.625rem]">
            <div className="flex items-center justify-between border-b border-border-divider pb-[0.375rem]">
              <div className="flex items-center gap-[0.5rem]">
                <Swords className="w-[1em] h-[1em] text-accent" />
                <h2 className="text-sm font-bold text-foreground-primary uppercase tracking-wider">
                  Follower Combat Buffs
                </h2>
                <span className="text-xs text-foreground-muted">
                  ({filteredFollower.length})
                </span>
              </div>

              <div className="text-[0.6875rem] text-foreground-muted flex items-center gap-[0.25rem]">
                <Info className="w-[0.9em] h-[0.9em]" />
                <span>Granted when married spouse travels with you as an active follower</span>
              </div>
            </div>

            {filteredFollower.length > 0 ? (
              <div className="grid grid-cols-1 lg:grid-cols-2 gap-[0.625rem] items-stretch">
                {filteredFollower.map((buff) => (
                  <BuffCard
                    key={buff.id}
                    buff={buff}
                    onNpcClick={openNpcDetail}
                  />
                ))}
              </div>
            ) : (
              <div className="p-[1.25rem] text-center bg-panel-inset/50 rounded border border-border-divider text-xs text-foreground-muted">
                No follower buffs match the current filter.
              </div>
            )}
          </section>
        </div>
      </ScrollArea>
    </div>
  )
}
