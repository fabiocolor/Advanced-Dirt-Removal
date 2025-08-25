/**
 * Sample script to demonstrate following operations in DaVinci Resolve Studio:
 * 1. Render current timeline using FCPX preset to additionally generate XML output.
 *
 * NOTE: Do the following before running this script:
 * 1. Ensure current project has at least 1 timeline present.
 * 2. Update 'TARGET_RENDER_DIR_FULL_PATH' in below script to appropriate path on your local machine.
 */

const WorkflowIntegration = require('./WorkflowIntegration.node');

// Configurable variables
PLUGIN_ID = "com.blackmagicdesign.resolve.scripttestplugin"; // update it to your unique plugin Id as mentioned in the manifest.xml file
TARGET_RENDER_DIR_FULL_PATH = ""; // update to some valid dir full path, example: "/Users/<user>/Media/Rendered" or "C:\\Users\\<user>\\Media\\Rendered"

// Main function to perform all actions
function main(pluginId, targetDirFullPath) {
    // Check pluginId
    if (!pluginId) {
        showAlert("Error: `pluginId` is empty, update it to some valid value and try again!");
        return false;
    }

    // Check targetDirFullPath
    if (!targetDirFullPath) {
        showAlert("Error: `targetDirFullPath` is empty, update it to some valid value and try again!");
        return;
    }

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

    // Get current project
    project = projectManager.GetCurrentProject();
    if (!project) {
        showAlert("Error: No current project exist, create a project and try again!");
        return;
    }

    // Get current timeline
    timeline = project.GetCurrentTimeline();
    if (!timeline) {
        showAlert("Error: No current timeline exist, add a timeline and try again!");
        return;
    }

    // Open Deliver page
    resolve.OpenPage("deliver");

    // Add render job in Deliver page and start rendering
    project.LoadRenderPreset("FCP - Final Cut Pro X");
    project.SetRenderSettings( {"TargetDir":targetDirFullPath} );
    project.AddRenderJob();
    project.StartRendering();

    // Done
    // WorkflowIntegration.CleanUp(); // Call CleanUp() during plugin app quit only
}

// Run main function
main(PLUGIN_ID, TARGET_RENDER_DIR_FULL_PATH);
