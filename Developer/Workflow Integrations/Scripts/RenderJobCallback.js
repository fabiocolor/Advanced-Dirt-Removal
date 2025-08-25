/**
 * Sample script to demonstrate following operations in DaVinci Resolve Studio:
 * 1. Register/Deregister render job start/stop JS callbacks
 * 2. JS Callback function invocation
 *
 * NOTE: Ensure current project has at least 1 render job present before running this script.
 */

const WorkflowIntegration = require('./WorkflowIntegration.node');

const RENDER_START_CALLBACK_NAME = 'RenderStart';
const RENDER_STOP_CALLBACK_NAME = 'RenderStop';

// Configurable variables
PLUGIN_ID = "com.blackmagicdesign.resolve.scripttestplugin"; // update it to your unique plugin Id as mentioned in the manifest.xml file

// Function to register callback
function registerCallback(callbackName, callbackFunction) {
    isCallbackRegistered = WorkflowIntegration.RegisterCallback(callbackName, callbackFunction);
    if (!isCallbackRegistered) {
        showAlert(`Error: Failed to register ${callbackName} callback!`);
    }

    return isCallbackRegistered;
}

// Function to deregister callback
function deregisterCallback(callbackName) {
    isCallbackDeregistered = WorkflowIntegration.DeregisterCallback(callbackName);
    if (!isCallbackDeregistered) {
        showAlert(`Error: Failed to deregister ${callbackName} callback!`);
    }

    return isCallbackDeregistered;
}

// Callback function for 'RenderStart'
function renderStartCallback() {
    showAlert("Rendering started");

    // Deregister render start callback
    deregisterCallback(RENDER_START_CALLBACK_NAME);
}

// Callback function for 'RenderStop'
function renderStopCallback() {
    showAlert("Rendering stopped");

    // Deregister render stop callback
    deregisterCallback(RENDER_STOP_CALLBACK_NAME);
}

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

    // Get current project
    projectManager = resolve.GetProjectManager();
    project = projectManager.GetCurrentProject();
    if (!project) {
        showAlert("Error: No current project exist, create a project and try again!");
        return;
    }

    // Get current timeline
    renderJobList = project.GetRenderJobList();
    if (!renderJobList || (renderJobList.length == 0)) {
        showAlert("Error: No render job exists in current project, add one render job and try again!");
        return;
    }

    // Open Deliver page
    resolve.OpenPage("deliver");

    // Register render start callback
    isCallbackRegistered = registerCallback(RENDER_START_CALLBACK_NAME, renderStartCallback);
    if (!isCallbackRegistered) return;

    // Register render stop callback
    isCallbackRegistered = registerCallback(RENDER_STOP_CALLBACK_NAME, renderStopCallback);
    if (!isCallbackRegistered) return;

    // Start rendering
    project.StartRendering();

    // Done
    // WorkflowIntegration.CleanUp(); // Call CleanUp() during plugin app quit only
}

// Run main function
main(PLUGIN_ID);
