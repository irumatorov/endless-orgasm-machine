// Copies the built UI into ../data/ui, gzipped, for "Upload Filesystem Image".
import { copyFileSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { gzipSync } from 'node:zlib'

const out = '../data/ui'
rmSync(out, { recursive: true, force: true })
mkdirSync(out, { recursive: true })

for (const file of readdirSync('dist')) {
  if (file === 'index.html') {
    const gz = gzipSync(readFileSync('dist/index.html'), { level: 9 })
    writeFileSync(`${out}/index.html.gz`, gz)
    console.log(`index.html.gz: ${(gz.length / 1024).toFixed(0)} KB`)
  } else {
    copyFileSync(`dist/${file}`, `${out}/${file}`)
  }
}
