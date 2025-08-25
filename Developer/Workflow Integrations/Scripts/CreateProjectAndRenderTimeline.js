/**
 * Sample script to demonstrate following operations in DaVinci Resolve Studio:
 * 1. Create a Project
 * 2. Open Media Page
 * 3. Add a clip in Media Pool
 * 4. Create timeline with added clip
 * 5. Open Deliver Page
 * 6. Add a render job
 * 7. Start rendering
 *
 * NOTE: Update below configurable variables to appropriate values/paths on your local machine to run the script successfully.
 */

const WorkflowIntegration = require('./WorkflowIntegration.node');

// Configurable variables
PLUGIN_ID = "com.blackmagicdesign.resolve.scripttestplugin"; // update it to your unique plugin Id as mentioned in the manifest.xml file
PROJECT_NAME = "CreateProjectAndRenderTimeline"; // make sure project doesn't exist with this name
TIMELINE_NAME = "Timeline_1";
VIDEO_CLIP_FULL_PATH = ""; // update to some valid video clip full path, example: "/Users/<user>/Media/SampleCLip.mov" or "C:\\Users\\<user>\\Media\\SampleCLip.mov"
TARGET_RENDER_DIR_FULL_PATH = ""; // update to some valid dir full path, example: "/Users/<user>/Media/Rendered" or "C:\\Users\\<user>\\Media\\Rendered"
RENDER_CLIP_NAME = "Rendered_Using_Plugin";

// Function to check argument value
function checkArgument(argName, argValue) {
    if (!argValue) {
        showAlert("Error: `argName` is empty, update it to some valid value and try again!");
        return false;
    }

    return true;
}

// Main function to perform all actions
function main(pluginId, projectName, timelineName, videoClipFullPath, targetDirFullPath, renderClipName) {
    // Check pluginId
    if (!checkArgument('pluginId', pluginId)) return;

    // Check projectName
    if (!checkArgument('projectName', projectName)) return;

    // Check timelineName
    if (!checkArgument('timelineName', timelineName)) return;

    // Check videoClipFullPath
    if (!checkArgument('videoClipFullPath', videoClipFullPath)) return;

    // Check targetDirFullPath
    if (!checkArgument('targetDirFullPath', targetDirFullPath)) return;

    // Check renderClipName
    if (!checkArgument('renderClipName', renderClipName)) return;

    // Initialize
    isInitialized = WorkflowIntegration.Initialize(pluginId);
    if (!isInitialized) {
        showAlert("Error: Failed to initialize!");
        return;
    }

    // Get resolve object
    resolve = WorkflowIntegration.GetResolve();
    if (!resolve) {
        showAlert("Error: Failed to get resolve object!");
        return;
    }

    // Get supporting objects
    projectManager = resolve.GetProjectManager();
    mediaStorage = resolve.GetMediaStorage();

    // Create project
    project = projectManager.CreateProject(projectName);
    if (!project) {
        showAlert(`Error: Failed to create project by name:"${projectName}", make sure project name is valid and it does not exist and then try again!`);
        return;
    }

    // Open media page
    resolve.OpenPage("media");

    // Add clip to MediaPool
    clips = mediaStorage.AddItemListToMediaPool(videoClipFullPath);
    if (!clips || !clips[0]) {
        showAlert("Error: Failed to add clip to MediaPool, check the value of `videoClipFullPath` in the script and try again!");
        return;
    }

    clip = clips[0];
    mediaPool = project.GetMediaPool();

    // Create Timeline with added clip
    timeline = mediaPool.CreateTimelineFromClips(timelineName, clip);
    if (!timeline) {
        showAlert("Error: Failed to create timeline!");
        return;
    }

    // Add render job in Deliver page and start rendering
    resolve.OpenPage("deliver");
    project.SetRenderSettings( {"TargetDir":targetDirFullPath, "CustomName":renderClipName} );
    project.AddRenderJob();
    project.StartRendering();

    // Done
    // WorkflowIntegration.CleanUp(); // Call CleanUp() during plugin app quit only
}

// Run main function
main(PLUGIN_ID, PROJECT_NAME, TIMELINE_NAME, VIDEO_CLIP_FULL_PATH, TARGET_RENDER_DIR_FULL_PATH, RENDER_CLIP_NAME);
