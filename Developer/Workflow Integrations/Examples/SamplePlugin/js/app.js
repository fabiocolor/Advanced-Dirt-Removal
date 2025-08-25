function Execute() {
    scriptText = document.getElementById("scriptTextArea").value;
    window.electronAPI.executeUserScript(scriptText);
}

function ClearScript() {
    document.getElementById("scriptTextArea").value = "";
}
