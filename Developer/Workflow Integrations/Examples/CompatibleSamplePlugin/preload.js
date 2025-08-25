// All of the Node.js APIs are available in the preload process.
// It has the same sandbox as a Chrome extension.

const WorkflowIntegration = require('./WorkflowIntegration.node')

const PLUGIN_ID = 'com.blackmagicdesign.resolve.compatiblesampleplugin'

// Exposing Resolve interface related functions here to renderer process.

window.GetResolveInterface = function GetResolveInterface() {
    // Initialize resolve interface
    const isResolveInit = WorkflowIntegration.Initialize(PLUGIN_ID)
    if (!isResolveInit) {
        alert('Error: Failed to initialize Resolve interface!')
        return null
    }

    // Get resolve interface object
    resolveInterfacObj = WorkflowIntegration.GetResolve();
    if (!resolveInterfacObj) {
        alert('Error: Failed to get Resolve interface object!')
        return null
    }

    return resolveInterfacObj
}

window.CleanupResolveInterface = function CleanupResolveInterface() {
    const isSuccess = WorkflowIntegration.CleanUp();
    if (!isSuccess) {
        console.log('Error: Failed to cleanup Resolve interface!');
    }
}
