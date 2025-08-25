// This file is required by the index.html file and will
// be executed in the renderer process for that window.

// Cached objects
let resolveObj = null;
let projectManagerObj = null;

// Perform action on DOM loaded.
window.addEventListener('DOMContentLoaded', () => {
    loadRenderPreset();
});

window.addEventListener('beforeunload', function (e) {
    CleanupResolveInterface();
});

// Gets resolve object.
function getResolve() {
    if (!resolveObj) {
        resolveObj = GetResolveInterface();
    }

    return resolveObj;
}

 // Gets project manager object.
function getProjectManager() {
    if (!projectManagerObj) {
        projectManagerObj = getResolve().GetProjectManager();
    }

    return projectManagerObj;
}

// Gets current project object.
function getCurrentProject() {
    return getProjectManager().GetCurrentProject();
}

// Gets media pool object.
function getMediaPool() {
    return getProjectManager().GetCurrentProject().GetMediaPool();
}

// Opens input page.
function openPage(page) {
    resolve = getResolve();

    if (resolve.GetCurrentPage() != page) {
        resolve.OpenPage(page);
    }
}

// Gets folder object by name.
function getFolder(rootBin, folderName) {
    if (!rootBin) return null;

    const folderList = rootBin.GetSubFolderList();
    if (!folderList || (folderList.length === 0)) return null;

    for (const folder of folderList) {
        if (folder.GetName() === folderName) return folder;
    }

    return null;
}

// Gets timeline object by name.
function getTimeline(project, timelineName) {
    if (!project) return null;

    const timelineCount = project.GetTimelineCount();
    if (!timelineCount || (timelineCount <= 0)) return null;

    for (let i = 1; i <= timelineCount; i++) {
        timeline = project.GetTimelineByIndex(i);
        if (timeline && (timeline.GetName() === timelineName)) return timeline;
    }

    return null;
}

// Load render preset list.
function loadRenderPreset() {
    var cbRenderPreset = document.getElementById('cbRenderPreset');

    if (!cbRenderPreset.options || (cbRenderPreset.options.length === 0)) {
        const project = getCurrentProject();
        const renderPresetList = project.GetRenderPresets();

        for (var index in renderPresetList) {
            option = document.createElement('option');
            option.text = renderPresetList[index];
            option.value = renderPresetList[index];
            cbRenderPreset.appendChild(option);
        }
    }
}

// Creates a new project.
function createProject() {
    const projName = document.getElementById('projName').value;
    if (!projName) {
        alert('Error: Project name is empty!');
        return;
    }

    projectManager = getProjectManager();
    if (!projectManager) {
        alert('Error: GetProjectManager() failed!');
        return;
    }

    project = projectManager.CreateProject(projName);
    if (!project) {
        alert(`Error: Failed to create project: ${projName}`);
        return;
    }

    const isSaved = projectManager.SaveProject();
    if (!isSaved) {
        alert(`Error: Failed to save project: ${projName}`);
        return;
    }

    console.log(`Created project: ${projName}`);
}

// Opens a project.
function openProject() {
    const projName = document.getElementById('projName').value;
    if (!projName) {
        alert('Error: Project name is empty!');
        return;
    }

    projectManager = getProjectManager();
    if (!projectManager) {
        alert('Error: GetProjectManager() failed!');
        return;
    }

    project = projectManager.LoadProject(projName);
    if (!project) {
        alert(`Error: Failed to open project: ${projName}`);
        return;
    }

    console.log(`Opened project: ${projName}`);
}

// Saves current project.
function saveProject() {
    projectManager = getProjectManager();
    if (!projectManager) {
        alert('Error: GetProjectManager() failed!');
        return;
    }

    const isSuccess = projectManager.SaveProject();
    if (!isSuccess) {
        alert('Error: Failed to save project');
        return;
    }

    console.log('Saved project');
}

// Creates a new bin.
function createBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    openPage('media');

    mediaPool = getMediaPool();
    rootBin = mediaPool.GetRootFolder();

    // Add new bin in root bin
    const bin = mediaPool.AddSubFolder(rootBin, binName);
    if (!bin) {
        alert(`Error: Failed to create bin: ${binName}`);
        return;
    }

    console.log(`Created bin: ${binName}`);
}

// Selects a bin.
function selectBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    openPage('media');

    mediaPool = getMediaPool();
    rootBin = mediaPool.GetRootFolder();

    const folderToSelect = getFolder(rootBin, binName);
    if (!folderToSelect) {
        alert(`Error: No folder exists with name: ${binName}`);
        return;
    }

    // Set current folder
    const isSuccess = mediaPool.SetCurrentFolder(folderToSelect);
    if (!isSuccess) {
        alert(`Error: Failed to select bin: ${binName}`);
        return;
    }

    console.log(`Selected bin: ${binName}`);
}

// Deletes a bin.
function deleteBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    openPage('media');

    mediaPool = getMediaPool();
    rootBin = mediaPool.GetRootFolder();

    const folderToDelete = getFolder(rootBin, binName);
    if (!folderToDelete) {
        alert(`Error: No folder exists with name: ${binName}`);
        return;
    }

    const folderList = [folderToDelete];
    const isSuccess = mediaPool.DeleteFolders(folderList);
    if (!isSuccess) {
        alert(`Error: Failed to delete folder with name: ${binName}`);
        return;
    }

    console.log(`Deleted bin: ${binName}`);
}

// Add clips to media pool.
function addClips() {
    // Get selected file paths
    const selectedFileInputs = document.getElementById('fileSelectClips');
    let filePathsArray = [];
    for (const fileInput of selectedFileInputs.files) {
        filePathsArray.push(fileInput.path);
    }

    if (filePathsArray.length === 0) {
        alert('Error: Empty file list detected!');
        return;
    }

    openPage('media');

    mediaStorage = getResolve().GetMediaStorage();

    // Add clips to the current folder
    const clips = mediaStorage.AddItemListToMediaPool(filePathsArray);
    if (!clips) {
        alert(`Error: Failed to add clips: ${filePathsArray}`);
        return;
    }

    console.log(`Added clips: ${filePathsArray}`);
}

// Creates a new timeline with all the clips in current folder.
function createTimeline() {
    const timelineName = document.getElementById('timelineName').value;
    if (!timelineName) {
        alert('Error: Timeline name is empty!');
        return;
    }

    openPage('media');

    mediaPool = getMediaPool();

    // Get current folder
    currentFolder = mediaPool.GetCurrentFolder();
    if (!currentFolder) {
        alert('Error: Failed to get current folder!');
        return;
    }

    // Get current folder clips
    const clips = currentFolder.GetClipList();
    if (!clips || (clips.length === 0)) {
        alert('Error: Failed to get current folder clips!');
        return;
    }

    // Create timeline with current folder clips
    const timeline = mediaPool.CreateTimelineFromClips(timelineName, clips);
    if (!timeline) {
        alert(`Error: Failed to create timeline: ${timelineName}`);
        return;
    }

    console.log(`Created timeline: ${timelineName}`);
}

// Selects a timeline using name.
function selectTimeline() {
    const timelineName = document.getElementById('timelineName').value;
    if (!timelineName) {
        alert('Error: Timeline name is empty!');
        return;
    }

    project = getCurrentProject();

    const timeline = getTimeline(project, timelineName);
    if (!timeline) {
        alert(`Error: No timeline exists with name: ${timelineName}`);
        return;
    }

    // Set current timeline
    const isSuccess = project.SetCurrentTimeline(timeline);
    if (!isSuccess) {
        alert(`Error: Failed to select timeline: ${timelineName}`);
        return;
    }

    console.log(`Selected timeline: ${timelineName}`);
}

// Render current timeline.
function renderTimeline() {
    const targetDirPath = document.getElementById('targetDirPath').value;
    if (!targetDirPath) {
        alert('Error: Target directory path is empty!');
        return;
    }

    const targetClipName = document.getElementById('targetClipName').value;
    if (!targetClipName) {
        alert('Error: Target clip name is empty!');
        return;
    }

    project = getCurrentProject();

    const timeline = project.GetCurrentTimeline();
    if (!timeline) {
        alert('Error: No timeline exists!');
        return;
    }

    openPage('deliver');

    // Load render preset
    const renderPresetName = document.getElementById('cbRenderPreset').value;
    if (renderPresetName && (renderPresetName.length > 0)) {
        isSuccess = project.LoadRenderPreset(renderPresetName);
        if (!isSuccess) {
            alert(`Error: Failed to load render preset: ${renderPresetName}`);
            return;
        }
    }

    // Set render settings
    isSuccess = project.SetRenderSettings({ 'TargetDir': targetDirPath, 'CustomName': targetClipName });
    if (!isSuccess) {
        alert('Error: Failed to set render settings!');
        return;
    }

    // Add render job
    const jobId = project.AddRenderJob();
    if (!jobId) {
        alert('Error: Failed to add render job!');
        return;
    }

    // Start rendering render job
    isSuccess = project.StartRendering(jobId);
    if (!isSuccess) {
        alert(`Error: Failed to render timeline: ${timelineName}`);
        return;
    }

    console.log(`Render timeline started: ${timelineName}`);
}
