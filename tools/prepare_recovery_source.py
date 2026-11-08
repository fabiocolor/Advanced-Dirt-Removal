"""Resolve menu tool: bind an aligned scan track to Advanced Dirt Removal on Edit/Color.

Install this file under Fusion/Scripts/Utility. Uses Resolve's embedded Python
and native source-capture OFX; no decoder, cleaning engine or Python package is
required. All preparation runs in a new duplicate timeline. Source tracks and
media in the operator's timeline remain intact.
"""
import hashlib
import json
import math
from pathlib import Path
import re
import time
import uuid


def integral(value, name):
    value = float(value)
    if not math.isfinite(value) or value != round(value) or abs(value) > 1e9:
        raise ValueError(name + ' must be a whole frame')
    return int(value)


def one_clip(timeline, track, first=None, end=None):
    items = timeline.GetItemListInTrack('video', track) or []
    if first is not None:
        items = [x for x in items if x.GetStart() <= first and x.GetEnd() >= end]
    if len(items) != 1:
        raise ValueError('Choose a track with one continuous clip covering the working export')
    return items[0]


def plan(resolve, working_track, source_track):
    project = resolve.GetProjectManager().GetCurrentProject()
    if not project or project.IsRenderingInProgress():
        raise ValueError('Open a project and wait for active rendering to finish')
    timeline = project.GetCurrentTimeline()
    if not timeline or working_track == source_track:
        raise ValueError('Choose two different video tracks')
    working = one_clip(timeline, working_track)
    first, end = integral(working.GetStart(), 'Working start'), integral(working.GetEnd(), 'Working end')
    if end <= first:
        raise ValueError('The working clip must contain at least one frame')
    source = one_clip(timeline, source_track, first, end)
    if source.GetFusionCompCount():
        raise ValueError('Use a scan track without an existing Fusion composition')
    width = integral(project.GetSetting('timelineResolutionWidth'), 'Timeline width')
    height = integral(project.GetSetting('timelineResolutionHeight'), 'Timeline height')
    fps = float(project.GetSetting('timelineFrameRate'))
    if project.GetSetting('colorScienceMode') != 'davinciYRGB':
        raise ValueError('This version requires the declared unmanaged Rec.709 RGB workflow; prepare matching derivatives explicitly')
    for item in (source, working):
        media = item.GetMediaPoolItem()
        if not media:
            raise ValueError('Use source-media clips, rather than compound or generated clips')
        resolution = tuple(map(int, re.findall(r'\d+', str(media.GetClipProperty('Resolution')))))
        if resolution != (width, height) or abs(float(media.GetClipProperty('FPS')) - fps) > 1e-8:
            raise ValueError('Scan, export and timeline must have matching native dimensions and frame rate')
        properties = item.GetProperty() or {}
        neutral = {'Pan': 0, 'Tilt': 0, 'ZoomX': 1, 'ZoomY': 1,
                   'RotationAngle': 0, 'AnchorPointX': 0, 'AnchorPointY': 0,
                   'Pitch': 0, 'Yaw': 0, 'FlipX': False, 'FlipY': False,
                   'CropLeft': 0, 'CropRight': 0, 'CropTop': 0, 'CropBottom': 0,
                   'Opacity': 100, 'CompositeMode': 0, 'Distortion': 0,
                   'DynamicZoomEnabled': False}
        if any(key not in properties or properties[key] != value for key, value in neutral.items()):
            raise ValueError('Use untransformed, uncropped, opaque clips; prepare matching derivatives explicitly')
        if integral(item.GetLeftOffset(), 'Left source trim') < 0 or integral(item.GetRightOffset(), 'Right source trim') < 0:
            raise ValueError('Clip trims extend beyond the source media; fix the edit before preparation')
        if integral(media.GetClipProperty('Frames'), 'Media frames') - integral(item.GetLeftOffset(), 'Left source trim') - integral(item.GetRightOffset(), 'Right source trim') != integral(item.GetDuration(), 'Duration'):
            raise ValueError('Retimed clips are unsupported; prepare a matching source derivative first')
    offset = integral(working.GetLeftOffset(), 'Working source offset')
    return {'project': project.GetName(), 'timeline': timeline.GetName(),
            'working_track': working_track, 'working_track_name': timeline.GetTrackName('video', working_track),
            'source_track': source_track, 'source_track_name': timeline.GetTrackName('video', source_track),
            'working_clip': working.GetName(), 'source_clip': source.GetName(),
            'record_first': first, 'record_end_exclusive': end, 'frame_count': end-first,
            'working_frame_first': offset, 'width': width, 'height': height, 'fps': fps,
            'source_record_first': integral(source.GetStart(), 'Scan start'),
            'working_media_id': working.GetMediaPoolItem().GetUniqueId(), 'source_media_id': source.GetMediaPoolItem().GetUniqueId(),
            'source_file': source.GetMediaPoolItem().GetClipProperty('File Path'),
            'source_track_initially_enabled': timeline.GetIsTrackEnabled('video', source_track),
            'source_clip_initially_enabled': source.GetClipEnabled(),
            'alignment': 'Operator-aligned track positions; no automatic retiming, registration, resizing or colour matching'}


def verify_source(folder, setup):
    records = []
    samples = setup['width'] * setup['height'] * 3
    for frame in range(setup['working_frame_first'], setup['working_frame_first'] + setup['frame_count']):
        path = folder / ('frame-' + str(frame) + '.adrs')
        with path.open('rb') as stream:
            if stream.readline().split() != [b'ADRS2', str(setup['width']).encode(), str(setup['height']).encode(), str(frame).encode()]:
                raise ValueError('Missing or mismatched prepared frame: ' + str(path))
            source_hash, clean_hash = stream.readline().strip(), stream.readline().strip()
            delta_size = int(stream.readline())
            if not 0 <= delta_size <= samples * 5 + 10:
                raise ValueError('Invalid prepared source size: ' + str(path))
            pixels = stream.read(samples * 4)
            delta = stream.read(delta_size)
            expected = bytearray([0]); value = samples
            while value >= 128:
                expected.append((value & 127) | 128); value >>= 7
            expected.append(value)
            if source_hash != clean_hash or len(pixels) != samples * 4 or hashlib.sha256(pixels).hexdigest().encode() != source_hash or delta != expected or stream.read(1):
                raise ValueError('Incomplete or corrupt prepared source: ' + str(path))
        records.append({'frame': frame, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    return records


def restore_view_state(resolve, timeline, page, timecode):
    """Allow queued Resolve page/seek updates to settle; observe the resulting state.

    Some versions return before the UI has applied a page change or seek. Avoid
    reopening an already active page, which can reset its viewer again.
    """
    attempts = []
    for _ in range(3):
        attempt = {'page_before': resolve.GetCurrentPage(),
                   'timecode_before': timeline.GetCurrentTimecode()}
        if attempt['page_before'] != page:
            attempt['open_page_return'] = resolve.OpenPage(page)
            for _ in range(20):
                time.sleep(.1)
                if resolve.GetCurrentPage() == page:
                    break
        if timeline.GetCurrentTimecode() != timecode:
            attempt['set_timecode_return'] = timeline.SetCurrentTimecode(timecode)
        consecutive_matches = 0
        for _ in range(20):
            time.sleep(.1)
            current_page = resolve.GetCurrentPage()
            current_timecode = timeline.GetCurrentTimecode()
            if current_page == page and current_timecode == timecode:
                consecutive_matches += 1
                if consecutive_matches >= 2:
                    break
            else:
                consecutive_matches = 0
        attempt['page_after'] = current_page
        attempt['timecode_after'] = current_timecode
        attempts.append(attempt)
        if consecutive_matches >= 2:
            break
    return {'page_after_restore': current_page,
            'timecode_after_restore': current_timecode,
            'view_restored': consecutive_matches >= 2,
            'attempts': attempts}


def prepare(resolve, working_track, source_track, destination, progress=lambda _: None):
    setup = plan(resolve, working_track, source_track)
    folder = Path(destination).resolve()
    folder.mkdir(parents=True, exist_ok=False)
    pm = resolve.GetProjectManager(); project = pm.GetCurrentProject(); original = project.GetCurrentTimeline()
    page, timecode = resolve.GetCurrentPage(), original.GetCurrentTimecode()
    setup['folder'] = str(folder)
    preset = 'Recovery setup restore ' + uuid.uuid4().hex[:12]
    preset_saved = False
    def receipt():
        (folder/'source-track.json').write_text(json.dumps(setup, indent=2) + '\n', encoding='utf-8')
    receipt()
    try:
        preset_saved = bool(project.SaveAsNewRenderPreset(preset))
        if not preset_saved:
            raise RuntimeError('Could not preserve the current Deliver settings')
        setup['saved_render_preset'] = preset
        if not pm.SaveProject():
            raise RuntimeError('Could not save the working project before preparing a source')
        copy = original.DuplicateTimeline('Recovery source prep ' + uuid.uuid4().hex[:10])
        if not copy or not project.SetCurrentTimeline(copy):
            raise RuntimeError('Could not create a separate preparation timeline')
        setup['preparation_timeline'] = copy.GetName()
        # Some Resolve builds acknowledge SetTrackEnable without applying it.
        # Remove other tracks only from this newly created preparation copy.
        for index in range(copy.GetTrackCount('video'), 0, -1):
            if index != source_track and not copy.DeleteTrack('video', index):
                raise RuntimeError('Could not isolate the source in the preparation copy')
        if copy.GetTrackCount('video') != 1 or not copy.GetIsTrackEnabled('video', 1):
            raise RuntimeError('Enable the scan video track before preparing it')
        item = one_clip(copy, 1, setup['record_first'], setup['record_end_exclusive'])
        if not item.SetClipEnabled(True) or not item.GetClipEnabled():
            raise RuntimeError('Could not enable the scan in the preparation copy')
        comp = item.AddFusionComp()
        if not comp:
            raise RuntimeError('Could not create the preparation processor')
        media_in, media_out = comp.FindTool('MediaIn1'), comp.FindTool('MediaOut1')
        capture = comp.AddTool('ofx.com.fabiocolor.adr.sourcecapture', 2, 0)
        if not capture or not media_in or not media_out:
            raise RuntimeError('Install the matching ADR Recovery Source Capture plugin and restart Resolve')
        attrs = comp.GetAttrs()
        shift = setup['working_frame_first'] + setup['source_record_first'] - setup['record_first'] - integral(attrs['COMPN_GlobalStart'], 'Composition start')
        for key, value in [('folder', str(folder)), ('frame_shift', shift), ('source_fps', setup['fps'])]:
            capture.SetInput(key, value)
            if capture.GetInput(key) != value:
                raise RuntimeError('Source preparation parameter was not accepted: ' + key)
        capture.ConnectInput('Source', media_in); media_out.ConnectInput('Input', capture)
        setup['composition_start'] = attrs['COMPN_GlobalStart']; setup['capture_frame_shift'] = shift
        render_dir = folder/'verification-export'; render_dir.mkdir()
        settings = {'TargetDir': str(render_dir), 'CustomName': 'scan-', 'SelectAllFrames': False,
                    'MarkIn': setup['record_first'], 'MarkOut': setup['record_end_exclusive']-1,
                    'ExportVideo': True, 'ExportAudio': False, 'FormatWidth': setup['width'], 'FormatHeight': setup['height']}
        if not project.SetCurrentRenderMode(1) or not project.SetCurrentRenderFormatAndCodec('exr', 'RGBFloatZIP') or not project.SetRenderSettings(settings):
            raise RuntimeError('Could not configure native float source preparation')
        setup['render_settings'] = settings; setup['job'] = project.AddRenderJob(); receipt()
        if not setup['job'] or not project.StartRendering([setup['job']], False):
            raise RuntimeError('Could not start source preparation')
        progress('Preparing the scan only. The working timeline is preserved.')
        while project.IsRenderingInProgress():
            time.sleep(.25)
        setup['render_status'] = project.GetRenderJobStatus(setup['job'])
        if setup['render_status'].get('JobStatus') != 'Complete':
            raise RuntimeError('Source preparation failed: ' + str(setup['render_status']))
        setup['frames'] = verify_source(folder, setup)
        if not project.SetCurrentTimeline(original):
            raise RuntimeError('Source is prepared but could not restore the working timeline')
        source_item = one_clip(original, source_track, setup['record_first'], setup['record_end_exclusive'])
        if not source_item.SetClipEnabled(False) or source_item.GetClipEnabled():
            raise RuntimeError('Source is prepared but could not disable its ordinary timeline output')
        setup['source_clip_recovery_only'] = True
        with (folder/'source-ready.txt').open('x', encoding='ascii') as marker:
            marker.write('ADRREADY1 %d %d\n' % (setup['working_frame_first'], setup['frame_count']))
        setup['verified'] = True
        progress('Source ready. On the working clip set Paint source to Prepared original, Auto recovery to Off and Original folder to: ' + str(folder))
        return setup
    except Exception as error:
        setup['error'] = str(error)
        raise
    finally:
        setup['working_timeline_restored'] = bool(project.SetCurrentTimeline(original))
        if setup.get('error') and setup['working_timeline_restored']:
            one_clip(original, source_track, setup['record_first'], setup['record_end_exclusive']).SetClipEnabled(setup['source_clip_initially_enabled'])
        if preset_saved:
            setup['render_settings_restored'] = bool(project.LoadRenderPreset(preset))
            if setup['render_settings_restored']:
                setup['temporary_preset_removed'] = bool(project.DeleteRenderPreset(preset))
        setup['view_restore'] = restore_view_state(resolve, original, page, timecode)
        setup['saved_after_preparation'] = bool(pm.SaveProject())
        receipt()
        failures = []
        if not setup['working_timeline_restored']:
            failures.append('the working timeline')
        if preset_saved and not setup.get('render_settings_restored'):
            failures.append('Deliver settings (the saved preset is retained)')
        if not setup['saved_after_preparation']:
            failures.append('saving the project')
        if failures:
            raise RuntimeError('Preparation evidence is retained, but could not restore ' + ', '.join(failures))


def main(resolve, fusion, bmd):
    ui = fusion.UIManager; dispatcher = bmd.UIDispatcher(ui)
    project = resolve.GetProjectManager().GetCurrentProject(); timeline = project and project.GetCurrentTimeline()
    if not timeline:
        raise ValueError('Open a timeline with a working export and an aligned original scan on separate tracks')
    tracks = list(range(1, timeline.GetTrackCount('video')+1))
    rows = [ui.Label({'Text': 'Choose the working export and its aligned recovery scan. Native size/rate must match.', 'WordWrap': True})]
    default_storage = str(Path.home()/'Movies'/'ADR Recovery Sources'/re.sub(r'[^\w .-]', '_', project.GetName()))
    selected = {'working': None, 'source': None}
    for track in tracks:
        rows.append(ui.HGroup([ui.Label({'Text': 'V%d  %s' % (track, timeline.GetTrackName('video', track))}),
                               ui.Button({'ID': 'Work%d'%track, 'Text': 'Working export', 'MinimumSize': [170,44]}),
                               ui.Button({'ID': 'Scan%d'%track, 'Text': 'Recovery scan', 'MinimumSize': [170,44]})]))
    rows.extend([ui.Label({'ID':'Selection', 'Text':'Select two different tracks.'}),
                 ui.Label({'Text':'Source storage (a new folder is created for each preparation):'}),
                 ui.LineEdit({'ID':'Storage', 'Text':default_storage, 'MinimumSize':[600,44]}),
                 ui.TextEdit({'ID':'Status','ReadOnly':True,'PlainText':'Preparation uses a new timeline and reads the scan track once. Keep the source folder with your project. Painting reads this folder without rerunning cleanup.'}),
                 ui.Button({'ID':'Prepare','Text':'Prepare recovery source','MinimumSize':[300,52]})])
    window = dispatcher.AddWindow({'ID':'ADRSecoverySource','WindowTitle':'Advanced Dirt Removal — Additional source','Geometry':[220,180,760,520]},ui.VGroup(rows))
    items = window.GetItems()
    def select(kind, index):
        selected[kind] = index
        for track in tracks:
            items['Work%d'%track].Text = ('● ' if selected['working']==track else '') + 'Working export'
            items['Scan%d'%track].Text = ('● ' if selected['source']==track else '') + 'Recovery scan'
        items['Selection'].Text = 'Working: %s   Recovery: %s' % (selected['working'], selected['source'])
    for track in tracks:
        window.On['Work%d'%track].Clicked = lambda event, index=track: select('working', index)
        window.On['Scan%d'%track].Clicked = lambda event, index=track: select('source', index)
    def execute(event):
        try:
            if not all(selected.values()):
                raise ValueError('Choose the working export and recovery scan tracks first')
            items['Prepare'].Enabled = False
            storage = items['Storage'].Text.strip()
            if not storage:
                raise ValueError('Choose a source storage folder')
            folder = Path(storage).expanduser()/('RecoverySource-'+uuid.uuid4().hex[:10])
            prepare(resolve, selected['working'], selected['source'], folder, lambda status: setattr(items['Status'], 'PlainText', status))
        except Exception as error:
            items['Status'].PlainText = str(error)
        finally:
            items['Prepare'].Enabled = True
    window.On.Prepare.Clicked = execute
    window.On.ADRSecoverySource.Close = lambda event: dispatcher.ExitLoop()
    window.Show(); dispatcher.RunLoop(); window.Hide()


if __name__ == '__main__':
    main(resolve, fusion, bmd)
