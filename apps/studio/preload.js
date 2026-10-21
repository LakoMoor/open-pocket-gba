// The renderer talks to the main process only through this API.
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('studio', {
  status: () => ipcRenderer.invoke('status'),
  installBuild: which => ipcRenderer.invoke('install-build', which),
  install: items => ipcRenderer.invoke('install', items),
  installRelease: () => ipcRenderer.invoke('install-release'),
  releaseInfo: () => ipcRenderer.invoke('release-info'),
  pick: kind => ipcRenderer.invoke('pick', kind),
  sd: (drive, source) => ipcRenderer.invoke('sd', drive, source),
  key: (k, down) => ipcRenderer.invoke('key', k, down),
  connect: () => ipcRenderer.invoke('connect'),
  shot: () => ipcRenderer.invoke('shot'),
  font: () => ipcRenderer.invoke('font'),
  open: url => ipcRenderer.invoke('open', url),
  on: (ch, fn) => ipcRenderer.on(ch, (_, d) => fn(d)),
});
