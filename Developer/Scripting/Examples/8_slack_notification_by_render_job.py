#!/usr/bin/env python

"""
    This is a sample slack notification script that is triggered by render job in the deliver page.
    It needs to be placed in the following OS specific directory:
    Mac OS X:   ~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Deliver
    Windows:    %APPDATA%\Blackmagic Design\DaVinci Resolve\Fusion\Scripts\Deliver\
    Linux:      /opt/resolve/Fusion/Scripts/Deliver/   (or /home/resolve/Fusion/Scripts/Deliver depending on installation)

    This trigger script will receive the following 3 global variables:
    "job" : job id.
    "status" : job status
    "error" : error string(if any)

    This script handles python 2.7 and 3.6. They uses different slack api library version. There is no lua equivalent script because slack does not have official lua api yet.

    These are the slack api guides for different python version:
    For python2.7: https://github.com/slackapi/python-slackclient/tree/v1
    For python3.6: https://github.com/slackapi/python-slackclient

    Before you can run this script,
    for python2.7, you need to install the necessary module for slackclient using the following command: pip install slackclient
    for python3.6, you need to install the necessary module for slackclient using the following command: pip3 install slackclient

    If you encounter ssl error for python3.6 in mac, you have to run "open /Applications/Python\ 3.6/Install\ Certificates.command" in command line

    Please replace the slack_token (i.e. <API_TOKEN_TO_REPLACE>) with API key in order to send notification.
    Please replace the channel_name (i.e. <CHANNEL_NAME_TO_REPLACE>") with the channel that you want to send to.
    A logfile called ScriptRenderLog.txt will be output in the same directory as where this python script is placed in.
"""

import os
import platform
import socket
import sys

# Different versions of python have different official library for slack.
if sys.version_info[0] == 2: #python2.7
    from slackclient import SlackClient
elif sys.version_info[0] == 3: #python3.6
    from slack import WebClient
    from slack.errors import SlackApiError

#slack token and channel name to send notification to.
slack_token = "<API_TOKEN_TO_REPLACE>" #Obtain from the slack api website.
channel_name = "<CHANNEL_NAME_TO_REPLACE>" #eg: #testing_channel

def getJobDetailsBasedOnId(project, jobId):
    jobList = project.GetRenderJobList()
    for jobDetail in jobList:
        if jobDetail["JobId"] == jobId:
            return jobDetail

    return ""

def notifySlack(message):
    if sys.version_info[0] == 2: #python2.7
        sc = SlackClient(slack_token)
        sc.api_call(
          "chat.postMessage",
          channel=channel_name,
          text=message
        )
    elif sys.version_info[0] == 3: #python3.6
        client = WebClient(token=slack_token)
        try:
            response = client.chat_postMessage(
                channel=channel_name,
                text=message
                )
        except SlackApiError as e:
            # You will get a SlackApiError if "ok" is False
            print("Error sending slack message: " + str(e.response['error']))

def main():
    project = resolve.GetProjectManager().GetCurrentProject()
    detailedStatus = project.GetRenderJobStatus(job)
    messageToSend = "Slack Message sent by hostname: {0}, project name: {1}, python version: {2}\n".format(socket.gethostname(), project.GetName(), platform.python_version())
    messageToSend += "Message initiated by: {0}\n".format(os.path.abspath(sys.argv[0]))
    messageToSend += "job id: {0}, job status: {1}, error (if any): \"{2}\"\n". format(job, status, error)
    messageToSend += "Detailed job status: {0}\n".format(str(detailedStatus))
    messageToSend += "Job Details: {0}\n".format(str(getJobDetailsBasedOnId(project, job)))
    notifySlack(messageToSend)

if __name__ == "__main__":
    main()
