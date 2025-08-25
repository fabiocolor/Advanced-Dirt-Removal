/**
 * Sample script to demonstrate following operations in DaVinci Resolve Studio:
 * 1. Add Bins to MediaPool
 * 2. Add clip to Bin
 *
 * NOTE: Update 'VIDEO_CLIP_FULL_PATH' in the below script text to appropriate path on your local machine to run the script successfully.
 */

const WorkflowIntegration = require('./WorkflowIntegration.node');

// Configurable variables
PLUGIN_ID = "com.blackmagicdesign.resolve.scripttestplugin"; // update it to your unique plugin Id as mentioned in the manifest.xml file
VIDEO_CLIP_FULL_PATH = ""; // update to some valid video clip full path, example: "/Users/<user>/Media/SampleCLip.mov" or "C:\\Users\\<user>\\Media\\SampleCLip.mov"

// Function to add bin to input parentBin
function AddBin(mediaPool, parentBin, binName) {
    return mediaPool.AddSubFolder(parentBin, binName);
}

// Function to add clip to a bin
function AddClipToBin(mediaStorage, mediaPool, clipFullPath, bin) {
    mediaPool.SetCurrentFolder(bin); // set the bin as current bin first
    mediaStorage.AddItemListToMediaPool(clipFullPath); // add clip to current bin
}

// Main function to perform all actions
function main(pluginId, videoClipFullPath) {
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

    // Add few bins in root bin
    bin1 = AddBin(mediaPool, rootBin, "Bin1"); // add Bin1 in root bin
    bin2 = AddBin(mediaPool, bin1, "Bin2"); // add Bin2 in Bin1

    // Add clip to bin
    if (videoClipFullPath) {
        mediaStorage = resolve.GetMediaStorage();

        AddClipToBin(mediaStorage, mediaPool, videoClipFullPath, bin2); // add clip in Bin2
    }

    // Done
    // WorkflowIntegration.CleanUp(); // Call CleanUp() during plugin app quit only
}

// Run main function
main(PLUGIN_ID, VIDEO_CLIP_FULL_PATH);
