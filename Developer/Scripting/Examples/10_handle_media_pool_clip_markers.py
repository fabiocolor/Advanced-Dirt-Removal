#!/usr/bin/env python

"""
Sample script to demonstrate following operations to first MediaPool clip in DaVinci Resolve Studio:
1. Add markers
2. Get markers
3. Set customData
4. Delete markers

NOTE: Add one clip (recommended clip duration >= 80 frames) in the MediaPool root bin before running this script.

Example usage: 10_media_pool_marker_operations.py
"""

import os
import sys
from python_get_resolve import GetResolve

resolve = GetResolve()
project = resolve.GetProjectManager().GetCurrentProject()

if not project:
    print("No project is loaded")
    sys.exit()

# Open Media page
resolve.OpenPage("media")

# Get supporting objects
projectManager = resolve.GetProjectManager()
mediaPool = project.GetMediaPool()
rootBin = mediaPool.GetRootFolder()

# Go to root bin
mediaPool.SetCurrentFolder(rootBin)

# Gets clips
clips = rootBin.GetClipList()
if not clips or not clips[0]:
    print("Error: MediaPool root bin doesn't contain any clips, add one clip (recommended clip duration >= 80 frames) and try again!")
    sys.exit()

# Choose first clip in the list
clip = clips[0]

# Get clip frames
framesProperty = clip.GetClipProperty("Frames")
if not framesProperty:
    print("Error: Failed to get clip 'Frames' property !")
    sys.exit()

numFrames = int(framesProperty)

# Add Markers
if (numFrames >= 1):
    isSuccess = clip.AddMarker(1, "Red", "Marker1", "Marker1 at frame 1", 1)
    if isSuccess:
        print("Added marker at FrameId:1")

if (numFrames >= 20):
    isSuccess = clip.AddMarker(20, "Blue", "Marker2", "Marker2 at frame 20", 1, "My Custom Data") # marker with custom data
    if isSuccess:
        print("Added marker at FrameId:20")

if (numFrames >= (50 + 20)):
    isSuccess = clip.AddMarker(50, "Green", "Marker3", "Marker3 at frame 50 (duration 20)", 20) # marker with duration 20 frames
    if isSuccess:
        print("Added marker at FrameId:50")

print("")

# Get all markers for the clip
markers = clip.GetMarkers()
for key, value in markers.items():
    print("Marker at FrameId:%d" % key)
    print(value)

print("")

# Get marker using custom data
markerWithCustomData = clip.GetMarkerByCustomData("My Custom Data")
print("Marker with customData:")
print(markerWithCustomData)

print("")

# Set marker custom data
updatedCustomData = "Updated Custom Data"
isSuccess = clip.UpdateMarkerCustomData(20, updatedCustomData)
if isSuccess:
    print("Updated marker customData at FrameId:20")

print("")

# Get marker custom data
customData = clip.GetMarkerCustomData(20)
print("Marker CustomData at FrameId:20 is:'%s'" % customData)

print("")

# Delete marker using color
isSuccess = clip.DeleteMarkersByColor("Red")
if isSuccess:
    print("Deleted marker with color:'Red'")

# Delete marker using frame number
isSuccess = clip.DeleteMarkerAtFrame(50)
if isSuccess:
    print("Deleted marker at frameNum:50")

# Delete marker using custom data
isSuccess = clip.DeleteMarkerByCustomData(updatedCustomData)
if isSuccess:
    print("Deleted marker with Customdata:'%s'" % updatedCustomData)
