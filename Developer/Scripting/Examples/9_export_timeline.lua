--[[
Example DaVinci Resolve script:
Exports timeline in different supported formats.
--]]


local function IsEmpty(arg)
    return arg == nil or arg == ''
end

local function Export(timeline, filePath, exportType, exportSubType)
    local result = false
    if IsEmpty(exportSubType) then
        result = timeline:Export(filePath, exportType)
    else
        result = timeline:Export(filePath, exportType, exportSubType)
    end

    if result then
        print("Timeline exported to " .. filePath .. " successfully.")
    else
        print("Timeline export failed.")
    end
end


resolve = Resolve()
projectManager = resolve:GetProjectManager()
project = projectManager:GetCurrentProject()
timeline = project:GetCurrentTimeline()

homeDir = os.getenv("HOME")
aafFilePath = homeDir .. "/sampleExp.aaf"
csvFilePath = homeDir .. "/sampleExp.csv"

Export(timeline, aafFilePath, resolve.EXPORT_AAF, resolve.EXPORT_AAF_NEW)
Export(timeline, csvFilePath, resolve.EXPORT_TEXT_CSV)

