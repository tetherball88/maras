import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { resolve, dirname } from 'path'
import { fileURLToPath } from 'url'
import fs from 'fs'
import YAML from 'yaml'

const __dirname = dirname(fileURLToPath(import.meta.url))

export default defineConfig({
  base: './',
  resolve: {
    alias: {
      '@': resolve(__dirname, './src'),
      '@locales-en': resolve(__dirname, '../SKSE/Plugins/MARAS/localization/en'),
    },
  },
  plugins: [
    react(),
    {
      name: 'maras-dev-yaml-and-images',
      apply: 'serve',
      configureServer(server) {
        server.middlewares.use((req, res, next) => {
          if (!req.url) return next()
          const cleanUrl = decodeURIComponent(req.url.split('?')[0])

          if (cleanUrl === '/__dev/trainingCatalog.json') {
            try {
              const yamlPath = resolve(__dirname, '../SKSE/Plugins/MARAS/childTraining.yaml')
              const parsed = YAML.parse(fs.readFileSync(yamlPath, 'utf8'))
              const locations = Array.isArray(parsed?.trainingLocations) ? parsed.trainingLocations : []
              const catalog = locations.map((loc: Record<string, any>, idx: number) => ({
                index: idx,
                id: loc.id,
                className: loc.name || loc.className || loc.id,
                location: loc.location || loc.locationName || '',
                description: loc.description || '',
                image: loc.image || '',
                costMultiplier: typeof loc.costMultiplier === 'number' ? loc.costMultiplier : 1.0,
                requiresBeastblood: Boolean(loc.requiresBeastblood),
                factionKey: loc.requirements?.key,
                factionLabel: loc.requirements?.label,
              }))
              res.setHeader('Content-Type', 'application/json')
              res.end(JSON.stringify(catalog))
              return
            } catch (err) {
              res.statusCode = 500
              res.end(JSON.stringify({ error: String(err) }))
              return
            }
          }

          if (cleanUrl.startsWith('/__dev/locale/')) {
            try {
              const reqLang = cleanUrl.replace('/__dev/locale/', '').trim() || 'en'
              const enLocRoot = resolve(__dirname, '../SKSE/Plugins/MARAS/localization')
              const packsLocRoot = resolve(__dirname, '../localization/packs')
              const flattenObj = (node: Record<string, any>, prefix = '', out: Record<string, string> = {}) => {
                for (const [k, v] of Object.entries(node || {})) {
                  if (!k || k.startsWith('$comment') || k.startsWith('_comment')) continue
                  const full = prefix ? `${prefix}.${k}` : k
                  if (typeof v === 'string') out[full] = v
                  else if (v && typeof v === 'object' && !Array.isArray(v)) flattenObj(v, full, out)
                }
                return out
              }
              const availableLanguages: any[] = []
              const seenCodes = new Set<string>()
              for (const locRoot of [enLocRoot, packsLocRoot]) {
                if (!fs.existsSync(locRoot)) continue
                for (const dirent of fs.readdirSync(locRoot, { withFileTypes: true })) {
                  if (!dirent.isDirectory() || dirent.name.startsWith('.')) continue
                  const metaPath = resolve(locRoot, dirent.name, '_meta.json')
                  let meta: any = { code: dirent.name, name: dirent.name, nativeName: dirent.name }
                  if (fs.existsSync(metaPath)) {
                    meta = { ...meta, ...JSON.parse(fs.readFileSync(metaPath, 'utf8')) }
                  }
                  const normCode = String(meta.code || dirent.name).toLowerCase()
                  if (seenCodes.has(normCode)) continue
                  seenCodes.add(normCode)
                  availableLanguages.push(meta)
                }
              }
              const targetCode = reqLang === 'auto' ? 'en' : reqLang
              const resolvedMeta =
                availableLanguages.find((m) => m.code.toLowerCase() === targetCode.toLowerCase()) ||
                availableLanguages.find((m) => m.code === 'en') || { code: 'en', name: 'English', nativeName: 'English' }
              const strings: Record<string, string> = {}
              const loadUiDir = (code: string) => {
                const searchRoots = code.toLowerCase() === 'en' ? [enLocRoot] : [packsLocRoot, enLocRoot]
                for (const locRoot of searchRoots) {
                  const dir = resolve(locRoot, code)
                  if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) continue
                  for (const f of fs.readdirSync(dir).sort()) {
                    if (!f.endsWith('.json') || f.startsWith('_') || f === 'names.json' || f.endsWith('_todo.json')) continue
                    const filePath = resolve(dir, f)
                    if (!fs.statSync(filePath).isFile()) continue
                    const raw = JSON.parse(fs.readFileSync(filePath, 'utf8'))
                    const flat = flattenObj(raw)
                    const isUiFile = f === 'ui.json' || f.startsWith('ui_') || f.startsWith('ui.')
                    for (const [k, v] of Object.entries(flat)) {
                      if (isUiFile) strings[k.startsWith('ui.') ? k.slice(3) : k] = v
                      else if (k.startsWith('ui.')) strings[k.slice(3)] = v
                    }
                  }
                  break
                }
              }
              loadUiDir('en')
              if (resolvedMeta.code !== 'en') loadUiDir(resolvedMeta.code)
              res.setHeader('Content-Type', 'application/json')
              res.end(
                JSON.stringify({
                  selectedLanguage: reqLang,
                  resolvedLanguage: resolvedMeta.code,
                  detectedSkyrimLanguage: 'en',
                  fontFamily: resolvedMeta.fontFamily || '',
                  availableLanguages,
                  strings,
                }),
              )
              return
            } catch (err) {
              res.statusCode = 500
              res.end(JSON.stringify({ error: String(err) }))
              return
            }
          }

          if (cleanUrl.startsWith('/Data/SKSE/Plugins/MARAS/')) {
            const relPath = cleanUrl.replace(/^\/Data\//, '')
            const diskPath = resolve(__dirname, '..', relPath)
            if (fs.existsSync(diskPath) && fs.statSync(diskPath).isFile()) {
              res.setHeader('Content-Type', diskPath.endsWith('.webp') ? 'image/webp' : 'application/octet-stream')
              fs.createReadStream(diskPath).pipe(res)
              return
            }
          }

          next()
        })
      },
    },
    {
      name: 'remove-module-attrs',
      apply: 'build',
      transformIndexHtml(html) {
        const scripts: string[] = []
        html = html
          .replace(/ type="module"/g, '')
          .replace(/ crossorigin/g, '')
          .replace(/<script\b[^>]*src="[^"]*"[^>]*><\/script>/g, (match) => {
            scripts.push(match)
            return ''
          })
        return html.replace('</body>', `  ${scripts.join('\n  ')}\n</body>`)
      }
    },
  ],
  build: {
    outDir: resolve(__dirname, '../Magelight/MARAS/views/MARAS'),
    emptyOutDir: true,
    minify: false,
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        format: 'iife',
      },
    },
  },
})
