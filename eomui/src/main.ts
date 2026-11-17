import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { Buffer } from 'buffer'

// The Buttplug client expects Node's Buffer.
(globalThis as any).Buffer = Buffer

export default mount(App, { target: document.getElementById('app')! })
