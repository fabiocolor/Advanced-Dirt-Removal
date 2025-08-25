--[[
Sample script to demonstrate following operations to first MediaPool clip in DaVinci Resolve Studio:
1. Add markers
2. Get markers
3. Set customData
4. Delete markers

NOTE: Add one clip (recommended clip duration >= 80 frames) in the MediaPool root bin before running this script.
--]]

local function GetFirstClipInMediaPool()
    -- Get supporting objects
    resolve = Resolve()
    projectManager = resolve:GetProjectManager()
    mediaPool = project:GetMediaPool()
    rootBin = mediaPool:GetRootFolder()

    -- Go to root bin
    mediaPool:SetCurrentFolder(rootBin)

    -- Get clips
    clips = rootBin:GetClipList()
    if not clips or (next(clips) == nil) then
        print("Error: MediaPool root bin doesn't contain any clips, add one clip (recommended clip duration >= 80 frames) and try again!")
        os.exit()
    end

    return clips[1]
end

local function GetClipFrameCount( clip )
    framesProperty = clip:GetClipProperty("Frames")
    if not framesProperty then
        print("Error: Failed to get clip 'Frames' property !")
        os.exit()
    end

    return tonumber(framesProperty)
end

local function AddMarkers( clip, numFrames )
    if (numFrames >= 1) then
        isSuccess = clip:AddMarker(1, "Red", "Marker1", "Marker1 at frame 1", 1)
        if isSuccess then
            print("Added marker at FrameId:1")
        end
    end

    if (numFrames >= 20) then
        isSuccess = clip:AddMarker(20, "Blue", "Marker2", "Marker2 at frame 20", 1, "My Custom Data") -- marker with custom data
        if isSuccess then
            print("Added marker at FrameId:20")
        end
    end

    if (numFrames >= (50 + 20)) then
        isSuccess = clip:AddMarker(50, "Green", "Marker3", "Marker3 at frame 50 (duration 20)", 20) -- marker with duration 20 frames
        if isSuccess then
            print("Added marker at FrameId:50")
        end
    end
end

local function PrintMarkerInfo( markerInfo )
    print("color:'" .. markerInfo["color"] .. "'  duration:" .. markerInfo["duration"] .. "  note:'" .. markerInfo["note"] .. "'  name:'" .. markerInfo["name"] .. "'  customData:'" .. markerInfo["customData"] .. "'")
end

local function PrintAllClipMarkers( clip )
    markers = clip:GetMarkers()
    for key, value in pairs(markers) do
        print("Marker at FrameId:" .. key)
        PrintMarkerInfo(value)
    end
end


resolve = Resolve()
project = resolve:GetProjectManager():GetCurrentProject()

if not project then
    print("No project is loaded")
    os.exit()
end

-- Open Media page
resolve:OpenPage("media")

-- Get first media pool clip
clip = GetFirstClipInMediaPool()

-- Get clip frames
numFrames = GetClipFrameCount(clip)

-- Add Markers
AddMarkers(clip, numFrames)

print("")

-- Get all markers for the clip
PrintAllClipMarkers(clip)

print("")

-- Get marker using custom data
markerWithCustomData = clip:GetMarkerByCustomData("My Custom Data")
print("Marker with customData:")
PrintMarkerInfo(markerWithCustomData)

print("")

-- Set marker custom data
updatedCustomData = "Updated Custom Data"
isSuccess = clip:UpdateMarkerCustomData(20, updatedCustomData)
if isSuccess then
    print("Updated marker customData at FrameId:20")
end

print("")

-- Get marker custom data
customData = clip:GetMarkerCustomData(20)
print("Marker CustomData at FrameId:20 is:'" .. customData .. "'")

print("")

-- Delete marker using color
isSuccess = clip:DeleteMarkersByColor("Red")
if isSuccess then
    print("Deleted marker with color:'Red'")
end

-- Delete marker using frame number
isSuccess = clip:DeleteMarkerAtFrame(50)
if isSuccess then
    print("Deleted marker at frameNum:50")
end

-- Delete marker using custom data
isSuccess = clip:DeleteMarkerByCustomData(updatedCustomData)
if isSuccess then
    print("Deleted marker with Customdata:'" .. updatedCustomData .. "'")
end
