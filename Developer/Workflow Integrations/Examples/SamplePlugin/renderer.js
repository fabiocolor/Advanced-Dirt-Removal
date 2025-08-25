// This file is required by the index.html file and will
// be executed in the renderer process for that window.

// Perform action on DOM loaded.
window.addEventListener('DOMContentLoaded', async () => {
    await loadRenderPreset();
});

// Loads render preset list.
async function loadRenderPreset() {
    var cbRenderPreset = document.getElementById('cbRenderPreset');

    if (!cbRenderPreset.options || (cbRenderPreset.options.length === 0)) {
        const renderPresetList = await window.resolveAPI.getRenderPresets();

        for (var index in renderPresetList) {
            option = document.createElement('option');
            option.text = renderPresetList[index];
            option.value = renderPresetList[index];
            cbRenderPreset.appendChild(option);
        }
    }
}

// Creates a new project.
async function createProject() {
    const projName = document.getElementById('projName').value;
    if (!projName) {
        alert('Error: Project name is empty!');
        return;
    }

    project = await window.resolveAPI.createProject(projName);
    if (!project) {
        alert(`Error: Failed to create project: ${projName}`);
        return;
    }

    const isSuccess = await window.resolveAPI.saveProject();
    if (!isSuccess) {
        alert(`Error: Failed to save project: ${projName}`);
        return;
    }

    console.log(`Created project: ${projName}`);
}

// Opens a project.
async function openProject() {
    const projName = document.getElementById('projName').value;
    if (!projName) {
        alert('Error: Project name is empty!');
        return;
    }

    project = await window.resolveAPI.openProject(projName);
    if (!project) {
        alert(`Error: Failed to open project: ${projName}`);
        return;
    }

    console.log(`Opened project: ${projName}`);
}

// Saves current project.
async function saveProject() {
    const isSuccess = await window.resolveAPI.saveProject();
    if (!isSuccess) {
        alert('Error: Failed to save project');
        return;
    }

    console.log('Saved project');
}

// Creates a new bin.
async function createBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    await window.resolveAPI.openPage('media');

    // Create new bin in root bin
    const bin = await window.resolveAPI.createBin(binName)
    if (!bin) {
        alert(`Error: Failed to create bin: ${binName}`);
        return;
    }

    console.log(`Created bin: ${binName}`);
}

// Selects a bin.
async function selectBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    await window.resolveAPI.openPage('media');

    const isSuccess = await window.resolveAPI.selectBin(binName)
    if (!isSuccess) {
        alert(`Error: Failed to select bin: ${binName}`);
        return;
    }

    console.log(`Selected bin: ${binName}`);
}

// Deletes a bin.
async function deleteBin() {
    const binName = document.getElementById('binName').value;
    if (!binName) {
        alert('Error: Bin name is empty!');
        return;
    }

    await window.resolveAPI.openPage('media');

    const isSuccess = await window.resolveAPI.deleteBin(binName)
    if (!isSuccess) {
        alert(`Error: Failed to delete bin: ${binName}`);
        return;
    }

    console.log(`Deleted bin: ${binName}`);
}

// Add clips to media pool.
async function addClips() {
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

    await window.resolveAPI.openPage('media');

    // Add clips to the current folder
    const clips = await window.resolveAPI.addClips(filePathsArray)
    if (!clips) {
        alert(`Error: Failed to add clips: ${filePathsArray}`);
        return;
    }

    console.log(`Added clips: ${filePathsArray}`);
}

// Creates a new timeline with all the clips in current folder.
async function createTimeline() {
    const timelineName = document.getElementById('timelineName').value;
    if (!timelineName) {
        alert('Error: Timeline name is empty!');
        return;
    }

    await window.resolveAPI.openPage('media');

    // Create timeline with current folder clips
    const timeline = await window.resolveAPI.createTimeline(timelineName);
    if (!timeline) {
        alert(`Error: Failed to create timeline: ${timelineName}`);
        return;
    }

    console.log(`Created timeline: ${timelineName}`);
}

// Selects a timeline using name.
async function selectTimeline() {
    const timelineName = document.getElementById('timelineName').value;
    if (!timelineName) {
        alert('Error: Timeline name is empty!');
        return;
    }

    // Set current timeline
    const isSuccess = await window.resolveAPI.selectTimeline(timelineName);
    if (!isSuccess) {
        alert(`Error: Failed to select timeline: ${timelineName}`);
        return;
    }

    console.log(`Selected timeline: ${timelineName}`);
}

// Render current timeline.
async function renderTimeline() {
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

    const renderPresetName = document.getElementById('cbRenderPreset').value;

    await window.resolveAPI.openPage('deliver');

    // Start rendering render job
    const isSuccess = await window.resolveAPI.renderTimeline(timelineName, renderPresetName, targetDirPath, targetClipName);
    if (!isSuccess) {
        alert(`Error: Failed to render timeline: ${timelineName}`);
        return;
    }

    console.log(`Render timeline started: ${timelineName}`);
}
