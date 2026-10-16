import { type ClassValue, clsx } from "clsx"
import { twMerge } from "tailwind-merge"

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}

/** True when running inside the Skyrim CEF host (production build). */
export const isInGame = (): boolean => (import.meta as any).env.PROD;
