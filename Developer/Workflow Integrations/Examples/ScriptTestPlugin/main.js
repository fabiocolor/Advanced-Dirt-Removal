// NOTE: Follow the security guide while implementing plugin app https://www.electronjs.org/docs/tutorial/security

// Modules to control application life and create native browser window.
const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');

// Cached objects
let mainWindow = null;

// Log messages on the renderer console.
function consoleLog(message) {
    mainWindow.webContents.executeJavaScript(`console.log('${message}');`);
}

// Show alert on the renderer process.
function showAlert(message) {
    mainWindow.webContents.executeJavaScript(`alert('${message}');`);
}

// Executes test script in the main process.
ipcMain.on('runTestScript', (event, testScript) => {
    eval(testScript);
})

function createWindow () {
    // Create the browser window.
    mainWindow = new BrowserWindow({
        width: 900,
        height: 825,
        useContentSize: true,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js')
        }
    });

    // Hide the menu bar (enable below code to hide menu bar)
    //mainWindow.setMenu(null);

    mainWindow.on('close', function(e) {
        app.quit();
    });

    // Load index.html on the window.
    mainWindow.loadFile('index.html');

    // Open the DevTools (enable below code to show DevTools)
    //mainWindow.webContents.openDevTools();
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', createWindow);

// Quit when all windows are closed.
app.on('window-all-closed', function () {
    // On macOS it is common for applications and their menu bar
    // to stay active until the user quits explicitly with Cmd + Q
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

// In this file you can include the rest of your plugin specific main process
// code. You can also put them in separate files and require them here.
