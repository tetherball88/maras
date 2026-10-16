import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { StatCards } from '../overview/StatCards'
import { HierarchyDisplay } from '../overview/HierarchyDisplay'
import { AffectionOverview } from '../overview/AffectionOverview'
import { ActivityFeed } from '../overview/ActivityFeed'
import { FullActivityFeed } from '../overview/FullActivityFeed'
import { NpcAllPairsList } from '../npcRelations/NpcAllPairsList'
import { NpcSelectorList } from '../npcRelations/NpcSelectorList'
import { NpcRelationshipDetails } from '../npcRelations/NpcRelationshipDetails'
import { ScrollArea } from '@/components/ui/scroll-area'

export const OverviewPage: React.FC = () => {
  const activeSubSection = useMarasStore((s) => s.activeSubSection)

  if (activeSubSection === 'activity') {
    return <FullActivityFeed />
  }

  if (activeSubSection === 'browse') {
    return (
      <div className="flex h-full w-full gap-[1rem] overflow-hidden">
        <NpcSelectorList />
        <NpcRelationshipDetails />
      </div>
    )
  }

  if (activeSubSection === 'allPairs') {
    return <NpcAllPairsList />
  }

  return (
    <ScrollArea className="h-full w-full pr-[0.5rem]">
      <div className="space-y-[1.25rem]">
        {/* Stat Summary Cards Row */}
        <StatCards />

        {/* Top 3 Spouse Hierarchy */}
        <HierarchyDisplay />

        {/* Two Column Layout: Affection Health Overview & Activity Feed */}
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-[1rem]">
          <AffectionOverview />
          <ActivityFeed limit={5} />
        </div>
      </div>
    </ScrollArea>
  )
}


