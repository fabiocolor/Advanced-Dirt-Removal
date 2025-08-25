// NOTE: Follow the security guide while implementing plugin app https://www.electronjs.org/docs/tutorial/security

// Modules to control application life and create native browser window.
const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const WorkflowIntegration = require('./WorkflowIntegration.node')

const PLUGIN_ID = 'com.blackmagicdesign.resolve.sampleplugin';

// Cached objects
let resolveObj = null;
let projectManagerObj = null;

// Initialize Resolve interface and returns Resolve object.
async function initResolveInterface() {
    // Initialize resolve interface
    const isSuccess = await WorkflowIntegration.Initialize(PLUGIN_ID);
    if (!isSuccess) {
        console.log('Error: Failed to initialize Resolve interface!');
        return null;
    }

    // Get resolve interface object
    resolveInterfacObj = await WorkflowIntegration.GetResolve();
    if (!resolveInterfacObj) {
        console.log('Error: Failed to get Resolve object!');
        return null;
    }

    return resolveInterfacObj
}

// Cleanup Resolve interface.
function cleanup() {
    const isSuccess = WorkflowIntegration.CleanUp();
    if (!isSuccess) {
        console.log('Error: Failed to cleanup Resolve interface!');
    }

    resolveObj = null;
    projectManagerObj = null;
}

// Gets Resolve object.
async function getResolve() {
    if (!resolveObj) {
        resolveObj = await initResolveInterface();
    }

    return resolveObj;
}

// Gets project manager object.
async function getProjectManager() {
    if (!projectManagerObj) {
        resolve = await getResolve();
        if (resolve) {
            projectManagerObj = await resolve.GetProjectManager();
            if (!projectManagerObj) {
                console.log('Error: Failed to get ProjectManager object!');
            }
        }
    }

    return projectManagerObj;
}

// Gets current project object.
async function getCurrentProject() {
    curProjManager = await getProjectManager();
    if (curProjManager) {
        currentProject = await curProjManager.GetCurrentProject();
        if (!currentProject) {
            console.log('Error: Failed to get current project object!');
        }

        return currentProject;
    }

    return null;
}

// Gets media pool object.
async function getMediaPool() {
    curProjManager = await getCurrentProject();
    if (curProjManager) {
        mediaPool = await curProjManager.GetMediaPool();
        if (!mediaPool) {
            console.log('Error: Failed to get MediaPool object!');
        }

        return mediaPool;
    }

    return null;
}

// Gets root folder object.
async function getRootFolder() {
    mediaPool = await getMediaPool();
    if (!mediaPool) return null;

    return await mediaPool.GetRootFolder();
}

// Gets folder object by name.
async function getFolder(rootBin, folderName) {
    if (!rootBin) return null;

    const folderList = await rootBin.GetSubFolderList();
    if (!folderList || (folderList.length === 0)) return null;

    for (const folder of folderList) {
        const tempFolderName = await folder.GetName();
        if (tempFolderName === folderName) return folder;
    }

    return null;
}

// Gets timeline object by name.
async function getTimeline(project, timelineName) {
    if (!project) return null;

    const timelineCount = await project.GetTimelineCount();
    if (!timelineCount || (timelineCount <= 0)) return null;

    for (let i = 1; i <= timelineCount; i++) {
        timeline = await project.GetTimelineByIndex(i);
        if (timeline && (await timeline.GetName() === timelineName)) {
            return timeline;
        }
    }

    return null;
}

// Opens input page.
async function openPage(event, pageName) {
    resolve = await getResolve();
    if (!resolve) return false;

    if (await resolve.GetCurrentPage() === pageName) return true;

    return await resolve.OpenPage(pageName);
}

// Creates a new project.
async function createProject(event, projectName) {
    projectManager = await getProjectManager();
    if (!projectManager) return null;

    return await projectManager.CreateProject(projectName);
}

// Saves current project.
async function saveProject() {
    projectManager = await getProjectManager();
    if (!projectManager) return false;

    return await projectManager.SaveProject();
}

// Opens a project.
async function openProject(event, projName) {
    projectManager = await getProjectManager();
    if (!projectManager) return null;

    return await projectManager.LoadProject(projName);
}

// Creates a new bin.
async function createBin(event, binName) {
    rootBin = await getRootFolder();
    if (!rootBin) return null;

    return await mediaPool.AddSubFolder(rootBin, binName);
}

// Selects a bin.
async function selectBin(event, binName) {
    rootBin = await getRootFolder();
    if (!rootBin) return null;

    const folderToSelect = await getFolder(rootBin, binName);
    if (!folderToSelect) return false;

    return await mediaPool.SetCurrentFolder(folderToSelect);
}

// Deletes a bin.
async function deleteBin(event, binName) {
    mediaPool = await getMediaPool();
    if (!mediaPool) return null;

    rootBin = await mediaPool.GetRootFolder();
    if (!rootBin) return null;

    const folderToDelete = await getFolder(rootBin, binName);
    if (!folderToDelete) return null;

    const folderList = [folderToDelete];
    return await mediaPool.DeleteFolders(folderList);
}

// Add clips to media pool.
async function addClips(event, filePathsArray) {
    resolve = await getResolve();
    if (!resolve) return null;

    mediaStorage = await resolve.GetMediaStorage();
    if (!mediaStorage) return null;

    return await mediaStorage.AddItemListToMediaPool(filePathsArray);
}

// Creates a new timeline with all the clips in current folder.
async function createTimeline(event, timelineName) {
    mediaPool = await getMediaPool();
    if (!mediaPool) return null;

    currentFolder = await mediaPool.GetCurrentFolder();
    if (!currentFolder) return null;

    // Get current folder clips
    clips = await currentFolder.GetClipList();
    if (!clips || (clips.length === 0)) return null;

    // Create timeline with clips
    return await mediaPool.CreateTimelineFromClips(timelineName, clips);
}

// Selects a timeline using name.
async function selectTimeline(event, timelineName) {
    project = await getCurrentProject();
    if (!project) return null;

    timeline = await getTimeline(project, timelineName);
    if (!timeline) return null;

    // Set current timeline
    return await project.SetCurrentTimeline(timeline);
}

// Render current timeline.
async function renderTimeline(event, timelineName, renderPresetName, targetDirPath, targetClipName) {
    project = await getCurrentProject();
    if (!project) return false;

    const timeline = await project.GetCurrentTimeline();
    if (!timeline) return false;

    // Set render settings
    const isSuccess = await project.SetRenderSettings({ "TargetDir": targetDirPath, "CustomName": targetClipName });
    if (!isSuccess) return false;

    // Add render job
    const jobId = await project.AddRenderJob();
    if (!jobId) return false;

    // Start rendering render job
    return await project.StartRendering(jobId);
}

// Gets render presets string list.
async function getRenderPresets() {
    project = await getCurrentProject();
    if (project) {
        return await project.GetRenderPresets();
    }

    return null;
}

// Register resolve event handler functions.
function registerResolveEventHandlers() {
    // Page
    ipcMain.handle('resolve:openPage', openPage);
    // Project
    ipcMain.handle('resolve:createProject', createProject);
    ipcMain.handle('resolve:saveProject', saveProject);
    ipcMain.handle('resolve:openProject', openProject);
    // Bin
    ipcMain.handle('resolve:createBin', createBin);
    ipcMain.handle('resolve:selectBin', selectBin);
    ipcMain.handle('resolve:deleteBin', deleteBin);
    // Clips
    ipcMain.handle('resolve:addClips', addClips);
    // Timeline
    ipcMain.handle('resolve:createTimeline', createTimeline);
    ipcMain.handle('resolve:selectTimeline', selectTimeline);
    // Render
    ipcMain.handle('resolve:renderTimeline', renderTimeline);
    // RenderPreset
    ipcMain.handle('resolve:getRenderPresets', getRenderPresets);
}

function createWindow () {
    // Create the browser window.
    mainWindow = new BrowserWindow({
        width: 900,
        height: 825,
        useContentSize: true,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js')
        }
    });

    // Hide the menu bar (enable below code to hide menu bar)
    //mainWindow.setMenu(null);

    mainWindow.on('close', function(e) {
        cleanup();
        app.quit();
    });

    // Load index.html on the window.
    mainWindow.loadFile('index.html');

    // Open the DevTools (enable below code to show DevTools)
    //mainWindow.webContents.openDevTools();
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(() => {
    registerResolveEventHandlers();
    createWindow();
});

// Quit when all windows are closed.
app.on('window-all-closed', function () {
    // On macOS it is common for applications and their menu bar
    // to stay active until the user quits explicitly with Cmd + Q
    if (process.platform !== 'darwin') {
        cleanup();
        app.quit();
    }
});

app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

// In this file you can include the rest of your plugin specific main process
// code. You can also put them in separate files and require them here.
