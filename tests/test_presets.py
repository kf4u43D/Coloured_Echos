"""Factory recipe consistency and safe deterministic generation contracts."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('generate_presets',ROOT/'tools/build/generate_presets.py')
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class FactoryBank(unittest.TestCase):
    def test_generated_bank_is_current_and_stores_musical_time(self):
        outputs=generator.bank_outputs()
        self.assertEqual(generator.stale_outputs(outputs),[])
        profiles={p:text for p,text in outputs.items() if p.suffix=='.profile'}
        self.assertEqual(len(profiles),17)
        for path,text in profiles.items():
            with self.subTest(profile=path.name):
                fields=dict(line.split('=',1) for line in text.splitlines() if line and not line.startswith('#'))
                self.assertNotIn('delay_ms',fields)
                self.assertEqual(fields['sync_mode'],'internal')
                self.assertEqual(fields['tempo_bpm'],'120')
                self.assertIn(fields['division'],generator.NOTES)
                self.assertEqual(fields['mod1_source'],'0')
                self.assertEqual(fields['mod2_source'],'0')
                if fields['modulation_enabled']=='0':self.assertEqual(fields['drift_amount'],'0')

    def test_each_nt_recipe_resets_sound_and_preserves_routing_and_manual_time(self):
        source=(ROOT/'src/nt/echo_lab.cpp').read_text(encoding='utf-8')
        enum=re.search(r'enum Parameter\s*\{(.*?)ParameterCount',source,re.S).group(1)
        all_parameters=set(re.findall(r'\b[A-Z][A-Za-z0-9_]*\b',enum))
        preserved={'InputL','InputR','OutputL','OutputModeL','OutputR','OutputModeR','Time',
                   'CV1Bus','CV2Bus','ClockBus','FactoryPreset'}
        required=all_parameters-preserved
        header=(ROOT/'src/nt/factory_presets.h').read_text(encoding='utf-8')
        arrays=re.findall(r'inline constexpr Value p\d+\[\]=\{(.*?)\n\};',header,re.S)
        self.assertEqual(len(arrays),17)
        for index,array in enumerate(arrays):
            with self.subTest(recipe=index):
                pairs=re.findall(r'\{(\w+),(-?\d+)\}',array)
                self.assertEqual(len(pairs),len(dict(pairs)))
                self.assertEqual(set(dict(pairs)),required)
                self.assertTrue(all(-32768<=int(value)<=32767 for _,value in pairs))
                self.assertEqual(dict(pairs)['SyncMode'],'3')
                self.assertEqual(dict(pairs)['TempoBpm'],'120')
        bank=json.loads((ROOT/'models/preset_bank.json').read_text(encoding='utf-8'))
        recipes={p['id']:{**bank['defaults'],**p['parameters']} for p in bank['presets']}
        self.assertEqual(recipes['tube_slap']['division'],'1/16')
        self.assertEqual(recipes['tube_slap']['feedback'],.10)
        self.assertEqual(recipes['bloom']['mix'],1)
        self.assertEqual(recipes['stereo_weave']['right_division'],'1/4T')
        self.assertEqual(recipes['stereo_weave']['division'],'1/4')
        self.assertEqual(recipes['stereo_weave']['model'],'stereo')
        self.assertEqual(recipes['stereo_weave']['feedback'],.32)
        self.assertEqual(recipes['stereo_weave']['stereo_ratio'],.67)
        self.assertLess(recipes['stereo_weave']['stereo_ratio'],1)
        self.assertAlmostEqual(recipes['stereo_weave']['stereo_ratio'],2/3,delta=.005)
        self.assertTrue(recipes['high_spark']['exciter_highpass_enabled'])
        self.assertEqual(recipes['high_spark']['feedback_highpass_hz'],300)
        self.assertFalse(any('against' in key for key in recipes))
        self.assertEqual(recipes['analog_bounce']['tone_hz'],3000)
        self.assertEqual(recipes['tape_space']['tone_hz'],4100)
        self.assertEqual(recipes['tape_space']['division'],'1/8')
        self.assertEqual(recipes['platter_motion']['tone_hz'],3200)
        self.assertEqual(recipes['platter_motion']['division'],'1/4')
        for key in ('analog_bounce','tape_space','platter_motion'):
            self.assertEqual(recipes[key]['drive'],.4)
            self.assertEqual(recipes[key]['mod_rate_hz'],1)
            self.assertEqual(recipes[key]['mod_depth_ms'],6)
        self.assertEqual(recipes['crushed_8bit']['tone_hz'],3500)
        self.assertEqual(recipes['crushed_8bit']['lores_cutoff_hz'],2000)
        self.assertFalse(recipes['crushed_8bit']['wet_tone_enabled'])

    def test_published_factory_slots_do_not_shift_when_recipes_retire(self):
        bank=json.loads((ROOT/'models/preset_bank.json').read_text(encoding='utf-8'))
        expected={'clean_quarter':1,'wide_dotted':2,'clocked_grain':3,'analog_bounce':4,
                  'tape_space':5,'tube_slap':6,'ducked_voice':7,'bloom':8,'reverse_pad':9,
                  'stereo_weave':10,'pingpong_width':11,'crushed_8bit':12,'high_spark':13,
                  'loop_bar':14,'filter_lift':18,'filter_fall':19,'platter_motion':20}
        self.assertEqual({p['id']:p['slot'] for p in bank['presets']},expected)
        self.assertEqual(bank['slot_count'],20)
        self.assertEqual(bank['retired_slots'],[15,16,17])
        header=(ROOT/'src/nt/factory_presets.h').read_text(encoding='utf-8')
        body=re.search(r'inline constexpr Preset presets\[slotCount\]=\{(.*?)\n\};',header,re.S).group(1)
        rows=[line.strip() for line in body.splitlines() if line.strip()]
        self.assertEqual(len(rows),20)
        for slot in bank['retired_slots']:self.assertEqual(rows[slot-1],'{"Custom",nullptr,0},')
        self.assertIn('"Filter Lift"',rows[17])
        self.assertIn('"Filter Fall"',rows[18])
        self.assertIn('"Platter Motion"',rows[19])
        active=re.search(r'activeSlots\[count\]=\{([^}]+)\}',header).group(1)
        self.assertEqual([int(slot) for slot in active.split(',')],sorted(expected.values()))

    def test_stale_profiles_header_and_extra_files_are_detected_without_writing(self):
        with tempfile.TemporaryDirectory(prefix='echolab-presets-') as temporary:
            root=Path(temporary)
            (root/'models').mkdir()
            (root/'models/preset_bank.json').write_bytes((ROOT/'models/preset_bank.json').read_bytes())
            outputs=generator.bank_outputs(root)
            self.assertEqual(len(generator.stale_outputs(outputs,root)),18)
            for path,text in outputs.items():
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(text,encoding='utf-8')
            self.assertEqual(generator.stale_outputs(outputs,root),[])
            changed=root/'models/presets/clean_quarter.profile'
            changed.write_text('feedback=0.99\n',encoding='utf-8')
            header=root/'src/nt/factory_presets.h'
            header.write_text('// stale\n',encoding='utf-8')
            extra=root/'models/presets/unlisted.profile'
            extra.write_text('mix=0\n',encoding='utf-8')
            self.assertEqual(set(generator.stale_outputs(outputs,root)),{
                'models/presets/clean_quarter.profile','models/presets/unlisted.profile','src/nt/factory_presets.h'})
            self.assertEqual(changed.read_text(),'feedback=0.99\n')
            self.assertEqual(header.read_text(),'// stale\n')


if __name__=='__main__':
    unittest.main()
