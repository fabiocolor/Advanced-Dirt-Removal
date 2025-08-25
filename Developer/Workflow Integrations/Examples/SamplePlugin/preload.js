// All of the Node.js APIs are available in the preload process.
// It has the same sandbox as a Chrome extension.

const { contextBridge, ipcRenderer } = require('electron/renderer')

contextBridge.exposeInMainWorld('resolveAPI', {
    // Page
    openPage: (pageName) => ipcRenderer.invoke('resolve:openPage', pageName),
    // Project
    createProject: (projectName) => ipcRenderer.invoke('resolve:createProject', projectName),
    saveProject: () => ipcRenderer.invoke('resolve:saveProject'),
    openProject: (projectName) => ipcRenderer.invoke('resolve:openProject', projectName),
    // Bin
    createBin: (binName) => ipcRenderer.invoke('resolve:createBin', binName),
    selectBin: (binName) => ipcRenderer.invoke('resolve:selectBin', binName),
    deleteBin: (binName) => ipcRenderer.invoke('resolve:deleteBin', binName),
    // Clips
    addClips: (filePathsArray) => ipcRenderer.invoke('resolve:addClips', filePathsArray),
    // Timeline
    createTimeline: (timelineName) => ipcRenderer.invoke('resolve:createTimeline', timelineName),
    selectTimeline: (timelineName) => ipcRenderer.invoke('resolve:selectTimeline', timelineName),
    // Render
    renderTimeline: (timelineName, renderPresetName, targetDirPath, targetClipName) => ipcRenderer.invoke('resolve:renderTimeline', timelineName, renderPresetName, targetDirPath, targetClipName),
    // RenderPreset
    getRenderPresets: () => ipcRenderer.invoke('resolve:getRenderPresets')
})
