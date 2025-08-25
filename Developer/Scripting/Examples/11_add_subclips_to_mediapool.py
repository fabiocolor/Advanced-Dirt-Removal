#!/usr/bin/env python

"""
Example DaVinci Resolve script:
Adds subclips [frame 0 .. 23] to current timeline for all media pool root folder clips
Example usage: 11_add_subclips_to_mediapool.py project1 24 1920 1080 /Users/username/Movies/sample.mov 0 23
"""

from python_get_resolve import GetResolve
import sys

# Inputs:
# - project name
# - project framerate
# - project width, in pixels
# - project height, in pixels
# - path to media
# - subclip start frame
# - subclip end frame
if len(sys.argv) < 8:
    print("input parameters for scripts are [project name] [framerate] [width] [height] [path to media] [start frame] [end frame]")
    sys.exit()

projectName = sys.argv[1]
framerate = sys.argv[2]
width = sys.argv[3]
height = sys.argv[4]
mediaPath = sys.argv[5]
startFrame = sys.argv[6]
endFrame = sys.argv[7]

# Create project and set parameters:
resolve = GetResolve()
projectManager = resolve.GetProjectManager()
project = projectManager.CreateProject(projectName)

if not project:
    print("Unable to create a project '" + projectName + "'")
    sys.exit()

project.SetSetting("timelineFrameRate", str(framerate))
project.SetSetting("timelineResolutionWidth", str(width))
project.SetSetting("timelineResolutionHeight", str(height))

# Add subclip to Media Pool:
subClip = {
    "media" : mediaPath,
    "startFrame": int(startFrame),
    "endFrame": int(endFrame),
}

resolve.GetMediaStorage().AddItemListToMediaPool([ subClip ])

projectManager.SaveProject()

print("'" + projectName + "' has been added")
