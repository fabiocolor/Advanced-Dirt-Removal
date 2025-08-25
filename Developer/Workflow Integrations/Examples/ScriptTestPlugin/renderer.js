// This file is required by the index.html file and will
// be executed in the renderer process for that window.

// Runs script.
function runScript() {
    testScript = document.getElementById('scriptTextArea').value;
    window.scriptTestAPI.runTestScript(testScript);
}

// Clears script text.
function clearScript() {
    document.getElementById("scriptTextArea").value = '';
}
