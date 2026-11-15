import React from 'react'
import { useMarasStore } from '../../store/useMarasStore'
import { PartnerList } from '../partners/PartnerList'
import { ChildrenList } from '../partners/ChildrenList'
import { HouseholdBuffsPage } from '../partners/HouseholdBuffsPage'

export const MyHouseholdPage: React.FC = () => {
  const activeSubSection = useMarasStore((s) => s.activeSubSection)

  const renderContent = () => {
    switch (activeSubSection) {
      case 'children':
        return <ChildrenList />
      case 'buffs':
        return <HouseholdBuffsPage />
      default:
        return <PartnerList />
    }
  }

  return (
    <div className="flex h-full w-full gap-[1rem] overflow-hidden">
      {renderContent()}
    </div>
  )
}
