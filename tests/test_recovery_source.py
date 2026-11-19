"""Preparation rejects ambiguous timing/transforms and validates every stored byte."""
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('source_setup', Path(__file__).parents[1]/'tools/prepare_recovery_source.py')
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class RecoverySource(unittest.TestCase):
    def fixture(self):
        resolve, project, timeline = Mock(), Mock(), Mock()
        resolve.GetProjectManager.return_value.GetCurrentProject.return_value = project
        project.IsRenderingInProgress.return_value = False
        project.GetCurrentTimeline.return_value = timeline
        settings = {'timelineResolutionWidth':'256', 'timelineResolutionHeight':'256',
                    'timelineFrameRate':'25', 'colorScienceMode':'davinciYRGB'}
        project.GetSetting.side_effect = settings.get
        neutral = dict(Pan=0,Tilt=0,ZoomX=1,ZoomY=1,RotationAngle=0,AnchorPointX=0,
                       AnchorPointY=0,Pitch=0,Yaw=0,FlipX=False,FlipY=False,
                       CropLeft=0,CropRight=0,CropTop=0,CropBottom=0,Opacity=100,
                       CompositeMode=0,Distortion=0,DynamicZoomEnabled=False)
        clips=[]
        for _ in range(2):
            item=Mock(); item.GetStart.return_value=100; item.GetEnd.return_value=125
            item.GetDuration.return_value=25; item.GetLeftOffset.return_value=0
            item.GetRightOffset.return_value=0; item.GetFusionCompCount.return_value=0
            item.GetProperty.return_value=neutral.copy()
            media={'Resolution':'256x256', 'FPS':'25', 'Frames':'25', 'File Path':'scan.exr'}
            item.GetMediaPoolItem.return_value.GetClipProperty.side_effect=media.get
            clips.append(item)
        timeline.GetItemListInTrack.side_effect=lambda kind,track:[clips[track-1]]
        return resolve, clips

    def test_one_to_one_native_plan(self):
        resolve,_=self.fixture()
        plan=source.plan(resolve,2,1)
        self.assertEqual((plan['frame_count'],plan['working_frame_first']),(25,0))

    def test_rejects_retime_transform_and_missing_transform_metadata(self):
        for change in ('retime','transform','unknown','negative_trim'):
            resolve,clips=self.fixture()
            if change=='retime': clips[0].GetDuration.return_value=24
            elif change=='transform': clips[0].GetProperty.return_value['ZoomX']=1.1
            elif change=='negative_trim': clips[0].GetLeftOffset.return_value=-1
            else: clips[0].GetProperty.return_value={}
            with self.assertRaises(ValueError): source.plan(resolve,2,1)

    def test_rejects_uncovered_or_multiple_source_clips(self):
        resolve,clips=self.fixture(); clips[0].GetEnd.return_value=124
        with self.assertRaises(ValueError): source.plan(resolve,2,1)
        resolve.GetProjectManager.return_value.GetCurrentProject.return_value.GetCurrentTimeline.return_value.GetItemListInTrack.side_effect=lambda *_:clips
        with self.assertRaises(ValueError): source.plan(resolve,2,1)

    def test_failed_preparation_retains_preset_if_deliver_restore_fails(self):
        resolve,clips=self.fixture()
        project=resolve.GetProjectManager.return_value.GetCurrentProject.return_value
        project.GetCurrentTimeline.return_value.DuplicateTimeline.return_value=None
        project.SaveAsNewRenderPreset.return_value=True
        project.LoadRenderPreset.return_value=False
        project.SetCurrentTimeline.return_value=True
        setup={'source_clip_initially_enabled':True,'record_first':100,'record_end_exclusive':125}
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)/'new-source'
            with patch.object(source,'plan',return_value=setup), patch.object(source,'restore_view_state',return_value={'view_restored':True}):
                with self.assertRaisesRegex(RuntimeError,'Deliver settings'):
                    source.prepare(resolve,2,1,folder)
            project.DeleteRenderPreset.assert_not_called()
            self.assertTrue((folder/'source-track.json').exists())
            self.assertFalse((folder/'source-ready.txt').exists())

    def test_verifies_payload_hash_frame_and_trailing_data(self):
        setup={'width':1,'height':1,'working_frame_first':0,'frame_count':1}
        pixels=b'\x00'*12; digest=hashlib.sha256(pixels).hexdigest().encode()
        pristine=b'ADRS2 1 1 0\n'+digest+b'\n'+digest+b'\n2\n'+pixels+b'\x00\x03'
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory); path=folder/'frame-0.adrs'; path.write_bytes(pristine)
            self.assertEqual(source.verify_source(folder,setup)[0]['frame'],0)
            for corrupt in (pristine+b'x',pristine[:-1],pristine.replace(b'1 1 0',b'1 1 1'),pristine[:-3]+b'\x01'+pristine[-2:]):
                path.write_bytes(corrupt)
                with self.assertRaises(ValueError): source.verify_source(folder,setup)


if __name__=='__main__': unittest.main()
