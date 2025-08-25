/**
 * Sample script to demonstrate following operations in DaVinci Resolve Studio:
 * 1. Delete all Bins from MediaPool
 * 2. Delete all clips from MediaPool
 *
 * NOTE: Add some Bins and Clips to MediaPool root bin before running this script.
 */

const WorkflowIntegration = require('./WorkflowIntegration.node');

// Configurable variables
PLUGIN_ID = "com.blackmagicdesign.resolve.scripttestplugin"; // update it to your unique plugin Id as mentioned in the manifest.xml file

// Main function to perform all actions
function main(pluginId) {
    // Check pluginId
    if (!pluginId) {
        showAlert("Error: `pluginId` is empty, update it to some valid value and try again!");
        return false;
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

    // Open Media page
    resolve.OpenPage("media");

    // Get supporting objects
    projectManager = resolve.GetProjectManager();
    project = projectManager.GetCurrentProject();
    mediaPool = project.GetMediaPool();
    rootBin = mediaPool.GetRootFolder();

    // Go to root bin
    mediaPool.SetCurrentFolder(rootBin);

    // Gets Bins
    bins = rootBin.GetSubFolderList();
    if (bins && bins[0]) {
        // Delete all bins
        mediaPool.DeleteFolders(bins);
    }

    // Gets clips
    clips = rootBin.GetClipList();
    if (clips && clips[0]) {
        // Delete all clips
        mediaPool.DeleteClips(clips);
    }

    // Done
    // WorkflowIntegration.CleanUp(); // Call CleanUp() during plugin app quit only
}

// Run main function
main(PLUGIN_ID);
