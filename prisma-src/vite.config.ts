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
    outDir: resolve(__dirname, '../PrismaUI/views/MARAS'),
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
