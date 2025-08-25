# Sample Workflow Integration script to Import Media and Link Proxy

import sys
from datetime import datetime

ui = fusion.UIManager
dispatcher = bmd.UIDispatcher(ui)

headerFont = ui.Font({ 'Family': 'Helvetica', 'PointSize': 16, 'Bold': True })

# For each functional example, we define the UI, add handlers and connect the UI to handlers
class AddOneMediaExample:
    def __init__(self):
        self.browse_id = f'browse_{0}'

    def get_ui(self):
        return ui.VGroup({ 'Weight': 0.1, 'Spacing': 5, }, [
            ui.Label({ 'Text': 'Import Clip to Current Media Bin', 'Font': headerFont, 'Weight': 0 }),
            ui.HGroup({ 'Weight': 0 }, [
                ui.Button({'ID': self.browse_id, 'Text': 'Choose Clip...', 'Geometry': [0, 0, 30, 50], 'Weight': 0.1}),
                ui.HGap(0, 0.9),
            ]),
            ui.VGap(0, 0.05)
        ])

    # handlers
    def file_from_browse(self, ev):
        selected_path = fusion.RequestFile()
        if selected_path:
            mediaPool = resolve.GetProjectManager().GetCurrentProject().GetMediaPool()
            mediaPool.ImportMedia(str(selected_path))

            proxyLinker = LinkProxyForLastClip()
            proxyLinker.refresh_helper()

    def connect_handlers(self, window):
        win.On[self.browse_id].Clicked = self.file_from_browse

class AddMultiMediaExample:
    def __init__(self):
        self.browse_id = f'browse_{1}'
        self.clear_id = f'clear'
        self.import_id = f'import'
        self.filepath_id = f'filepath'
        self.paths = []

    def get_ui(self):
        return ui.VGroup({ 'Weight': 0.5, 'Spacing': 5, }, [
            ui.Label({ 'Text': 'Import Multiple Selections to Current Media Bin', 'Font': headerFont, 'Weight': 0 }),
            ui.HGroup({'Weight': 0}, [
                ui.Button({'ID': self.browse_id, 'Text': 'Add Clips...', 'Geometry': [0, 0, 30, 50], 'Weight': 0}),
                ui.HGap(0, 0.1),
                ui.Button({'ID': self.clear_id, 'Text': 'Clear', 'Geometry': [0, 0, 30, 50], 'Weight': 0}),
                ui.HGap(0, 0.9),
            ]),
            ui.TextEdit({'ID': self.filepath_id, 'PlaceholderText': 'No Clips Selected', 'ReadOnly': True, 'Weight': 0.9}),
            ui.HGroup({ 'Weight': 0 }, [
                ui.Button({ 'ID': self.import_id,  'Text': "Import Clips in List", 'Geometry': [0, 0, 30, 50], 'Weight': 0.1}),
                ui.HGap(0, 0.9),
            ]),
            ui.VGap(0, 0.05)
        ])

    def connect_handlers(self, window):
        win.On[self.browse_id].Clicked = self.files_from_browse
        win.On[self.clear_id].Clicked = self.clear
        win.On[self.import_id].Clicked = self.import_files

    # handlers
    def files_from_browse(self, ev):
        selectedPaths = fusion.RequestFile("", "", {'FReqB_Multi':True}) # takes in the arguments RequestFile(<path>, <file>, <options>)
        self.convert_dict_to_paths(selectedPaths)

        win.GetItems()[self.filepath_id].PlainText = '\n'.join(self.paths)

    def convert_dict_to_paths(self, selectedPaths):
        pathDict = eval(str(selectedPaths))
        commonParentPath = pathDict['Path']
        pathDict.pop('Path', None)
        for childPath in pathDict.values():
            self.paths.append(commonParentPath + childPath)

    def clear(self, ev):
        self.paths.clear()
        win.Find(self.filepath_id).PlainText = ""

    def import_files(self, ev):
        mediaPool = resolve.GetProjectManager().GetCurrentProject().GetMediaPool()
        mediaPool.ImportMedia(self.paths)

        proxyLinker = LinkProxyForLastClip()
        proxyLinker.refresh_helper()

class LinkProxyForLastClip:
    def __new__(cls):
        if not hasattr(cls, 'instance'):
            cls.instance = super(LinkProxyForLastClip, cls).__new__(cls)
        return cls.instance

    def __init__(self):
        self.browse_id = f'browse_{2}'
        self.refresh_id = f'refresh'
        self.clip_path_id = f'clip_path'

    def get_ui(self):
        return ui.VGroup({ 'Weight': 0, 'Spacing': 5, }, [
            ui.Label({ 'Text': 'Link Proxy For Clip', 'Font': headerFont, 'Weight': 0 }),
            ui.HGroup( {'Weight': 0 }, [
                ui.Label({ 'Text': 'Last Added Clip: ', 'Weight': 0 }),
                ui.LineEdit({'ID': self.clip_path_id, 'ReadOnly': True, 'Weight': 0.9})
            ]),
            ui.HGroup( {'Weight': 0 }, [
                ui.Button({ 'ID': self.refresh_id,  'Text': "Refresh last added clip", 'Geometry': [0, 0, 30, 50], 'Weight': 0}),
                ui.Button({ 'ID': self.browse_id,  'Text': "Link Proxy...", 'Geometry': [0, 0, 30, 50], 'Weight': 0}),
                ui.HGap(0, 0.8),
            ])
        ])

    def connect_handlers(self, window):
        win.On[self.browse_id].Clicked = self.files_from_browse
        win.On[self.refresh_id].Clicked = self.refresh

    def files_from_browse(self, ev):
        if win.Find(self.clip_path_id).Text:
            selectedPath = fusion.RequestFile()
            if selectedPath:
                clipToLink.LinkProxyMedia(str(selectedPath))

    def refresh(self, ev):
        self.refresh_helper()

    def refresh_helper(self):
        clips = resolve.GetProjectManager().GetCurrentProject().GetMediaPool().GetCurrentFolder().GetClipList()
        global clipToLink
        if clips:
            dateTimes = [LinkProxyForLastClip.convert_to_datetime(clip) for clip in clips]
            clipToLink = clips[dateTimes.index(max(dateTimes))]
            win.Find(self.clip_path_id).Text = clipToLink.GetClipProperty("File Path")
        else:
            win.Find(self.clip_path_id).Text = ""
            clipToLink = None

    @staticmethod
    def convert_to_datetime(clip):
        dateAdded = clip.GetClipProperty("Date Added")
        return datetime.strptime(dateAdded, '%a %b %d %Y %H:%M:%S')

win_id = "com.blackmagicdesign.resolve.WorkflowIntegrationPythonExample" # unique if this integration is run as a single instance

# check for existing instance
win = ui.FindWindow(win_id)
if win:
	win.Show()
	win.Raise()
	exit()

default_config = {
   "window": {
       "ID": win_id,
       "Geometry": [100, 100, 600, 500],
       "WindowTitle" : "Example DaVinci Resolve Media Toolkit",
   }
}

examples = [AddOneMediaExample(), AddMultiMediaExample(), LinkProxyForLastClip()]
win = dispatcher.AddWindow(default_config['window'], ui.VGroup({ 'Spacing': 5, }, [
        example.get_ui() for example in examples
    ])
)

def close(ev):
    dispatcher.ExitLoop()

win.On[win_id].Close = close

for example in examples:
    example.connect_handlers(win)

win.Show()
LinkProxyForLastClip().refresh_helper()
dispatcher.RunLoop()
