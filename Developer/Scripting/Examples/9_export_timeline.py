#!/usr/bin/env python

"""
Example DaVinci Resolve script:
Exports timeline in different supported formats.
Example usage: 9_export_timeline.py
"""

import os
import sys
from python_get_resolve import GetResolve


def Export(timeline, filePath, exportType, exportSubType=None):
    result = None
    if exportSubType is None:
        result = timeline.Export(filePath, exportType)
    else:
        result = timeline.Export(filePath, exportType, exportSubType)

    if result:
        print("Timeline exported to {0} successfully.".format(filePath))
    else:
        print("Timeline export failed.")


resolve = GetResolve()
project = resolve.GetProjectManager().GetCurrentProject()

if not project:
    print("No project is loaded")
    sys.exit()

aafFilePath = os.path.join(os.path.expanduser("~"), "sampleExp.aaf")
csvFilePath = os.path.join(os.path.expanduser("~"), "sampleExp.csv")

timeline = project.GetCurrentTimeline()
Export(timeline, aafFilePath, resolve.EXPORT_AAF, resolve.EXPORT_AAF_NEW)
Export(timeline, csvFilePath, resolve.EXPORT_TEXT_CSV)

