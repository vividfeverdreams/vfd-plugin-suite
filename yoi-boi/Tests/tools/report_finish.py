#!/usr/bin/env python3
"""Assemble the 0.3.0 evidence report; optionally cross-check all WAVs with ffmpeg."""
import argparse, os, shutil, concurrent.futures, csv, hashlib, json, re, subprocess
from pathlib import Path
import numpy as np

def table(path, key):
    return {x[key]:x for x in csv.DictReader(path.open(), delimiter='\t') if x.get(key) and x[key] != 'ROLLS'}
def crosscheck(root):
    ffmpeg=os.environ.get('FFMPEG') or shutil.which('ffmpeg') or 'ffmpeg'  # set FFMPEG=/path/to/ffmpeg if it isn't on PATH
    jobs=[p for side in ['before','after'] for p in sorted((root/side).glob('[0-9]*.wav'))]
    def one(p):
        out=subprocess.run([ffmpeg,'-hide_banner','-nostats','-i',str(p),'-af','ebur128=peak=true:framelog=verbose','-f','null','-'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,check=True).stderr
        lufs=float(re.findall(r'Integrated loudness:\s*I:\s*([-\d.]+)',out)[-1])
        tp=float(re.findall(r'True peak:\s*Peak:\s*([-\d.]+)',out)[-1])
        return {'side':p.parent.name,'file':p.name,'LUFS_I':lufs,'true_peak_dBTP':tp}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: rows=list(pool.map(one,jobs))
    (root/'ffmpeg-crosscheck.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('ffmpeg cross-checked',len(rows),'files')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('root',type=Path); ap.add_argument('--crosscheck',action='store_true'); ap.add_argument('--docs',action='store_true'); args=ap.parse_args();root=args.root
    if args.crosscheck:crosscheck(root)
    before=table(root/'before-measured.tsv','file');after=table(root/'after-measured-final.tsv','file')
    spectral=json.loads((root/'spectral.json').read_text()); assert len(before)==len(after)==len(spectral)==87
    roll=[x for x in csv.DictReader((root/'rolls-final.tsv').open(),delimiter='\t') if x['seed'].isdigit()];assert len(roll)==200
    phrase=[x for x in csv.DictReader((root/'after-render-final.log').open(),delimiter='\t') if x.get('preset') and x.get('engine')]
    folders={x['preset']:x['folder'] for x in phrase}; assert len(folders)==87
    def vals(rows,key):return np.array([float(x[key]) for x in rows])
    med=lambda x:float(np.median(x))
    oldl=vals(before.values(),'LUFS_I');newl=vals(after.values(),'LUFS_I');oldtp=vals(before.values(),'true_peak_dBTP');newtp=vals(after.values(),'true_peak_dBTP')
    vc0=med([x['before_contrast'] for x in spectral]);vc1=med([x['after_contrast'] for x in spectral]);worst=min(x['contrast_change_pct'] for x in spectral)
    rolls_l=vals(roll,'LUFS_I');rolls_tp=vals(roll,'true_peak_dBTP')
    assert np.max(np.abs(newl+10))<=1 and max(newtp)<=-1 and vc1>=vc0 and worst>=-10
    assert max(np.abs(rolls_l+10))<=2 and max(rolls_tp)<=-1
    ff=json.loads((root/'ffmpeg-crosscheck.json').read_text())
    differences=[abs(x['LUFS_I']-float((before if x['side']=='before' else after)[x['file']]['LUFS_I'])) for x in ff]
    tp_differences=[abs(x['true_peak_dBTP']-float((before if x['side']=='before' else after)[x['file']]['true_peak_dBTP'])) for x in ff]
    assert max(differences)<.11 and len(ff)==174
    text=[ '# YOI-BOI 0.3.0 measurements — 2026-11-11', '',
        f'87/87 factory sounds pass: **{min(newl):.3f} to {max(newl):.3f} LUFS integrated**, maximum true peak **{max(newtp):.3f} dBTP**. Bank median: {med(oldl):.3f} → {med(newl):.3f} LUFS ({med(newl)-med(oldl):+.2f} LU).', '',
        f'Vowel/formant contrast median: **{vc0:.4f} → {vc1:.4f} dB** ({100*(vc1/vc0-1):+.2f}%). Worst individual change: **{worst:.2f}%**. All 87 stay above the −10% limit.', '',
        f'200/200 fresh-default NEW YOI seeds pass: **{min(rolls_l):.3f} to {max(rolls_l):.3f} LUFS**, median {med(rolls_l):.3f}; maximum true peak **{max(rolls_tp):.3f} dBTP**. Seeds 1–200; all four generation families. The die selects this same calibrated factory bank through the preset-loading path.', '',
        '## Scope and reproducibility','',
        '- Before rebuilding, the current test executable was copied to `before-bin/YoiBoiTests` and used for `--render-demos before/`. Its companion VST3 was also preserved. The pre-existing binary identifies itself as **0.2.4**, despite the checked-in CMake version being 0.2.5. Those requested before renders remain intact.',
        '- A separate 0.2.5 reference VST3 was built from untouched git source **e11fd98** under `work/yoi-boi/build/reference-025-src/`; only build resource paths were adapted. The 0.2.4→0.2.5 DSP/patch/parameter-mapping diff is empty; 0.2.5 restored the MOD Release control. Its reference bundle is copied into `before-bin/source-0.2.5/`.',
        '- Standard phrase: 48 kHz stereo, 256-frame blocks, 140 BPM; MIDI G2/G2/B-flat2/G2 (43/43/46/43), velocity 112, four notes over 3 seconds, with the original wheel/pressure sweeps. No playback, windows, editors, plug-in installation, commits, or pushes occurred.',
        '- The original console render path keeps default Length/Sub locks during preset loading. The after path retains this protocol. Factory loading now measures the resolved variant when locks change its sound, so the baked authored trim stays useful with player locks. The generated table also measures each fully authored preset independently.',
        '- BS.1770-4 stereo K weighting uses double precision and 400 ms windows/100 ms hops, with −70 LUFS absolute and −10 LU relative gates. The console tool replaces the old ungated “LUFSish” value. WAV true peaks use 4×, 64-tap Blackman-windowed sinc reconstruction.',
        f'- Independent ffmpeg ebur128 cross-check: **174/174 WAVs**; maximum integrated discrepancy {max(differences):.4f} LU and true-peak discrepancy {max(tp_differences):.4f} dB (ffmpeg prints 0.1-unit precision). See `ffmpeg-crosscheck.json`.',
        '- Band levels are full-render stereo-mean RMS power in dBFS, integrated by Parseval/FFT. They are not LUFS. `spectral.json` also includes below-30 and 40–120 Hz power.',
        '- Vowel contrast is a level-independent spectral-envelope measure: 4096-frame Hann windows, 2048-frame hop, 1/6-octave bands centred at 300–3809 Hz, active-note frame centres only. Remove the mean and linear log-frequency tilt of each log-power envelope; take its residual RMS in dB, then the median over frames. This measures formant peak/valley definition, not speech recognition or a listening-test claim. Source: `Tests/tools/analyse_finish.py`; inspired by SHIFTY’s envelope/contrast A/B workflow.', '',
        '## DSP and compatibility decisions','',
        '- FINISH ports SHIFTY SMASH’s phase-aligned LR4 three-band split, gated program-relative RMS thresholds, 9 dB knee, opto-like release memory and control-rate interpolated gain computer. The independent sub never enters its detectors/upward compression. Band saturation was omitted to preserve vowel contrast.',
        '- Body OTT → sub sum (or separate Sub bus) → SUSTAINABLE console-memory compression and biased, drive-compensated soft curve → smooth eighth-order ceiling → 18 Hz DC blocker → reconstruction guard. Engines 3/4 reuse their existing 4× loop; engines 1/2 use a 2× half-band wrapper around frozen archive code. A sub-free synchronous archive shadow supplies their body; the sub/master-shaper residual bypasses OTT.',
        '- Engine 4’s sub follows the played note/bend/glide, independently of the body pitch envelope. Below 30 Hz it fades out without transposing. Its body low cut follows the lowest held/sustained note at sqrt(2)×f0, smoothed through glide and clamped to 35–350 Hz. Classic retains octave-down sub pitch and fixed 110 Hz body cut.',
        '- New FINISH sounds initialise their first-note smoothed FM controls deterministically. Previously a preceding preset could change the persistent modulator/carrier phase and invalidate a level trim. FINISH-off legacy behaviour stays bit-identical. Engine-4 vowel resonance has a small FINISH-only emphasis; Space Bass Cadet received extra authored resonance. Preset-specific OTT/sub balance protects quieter vowels from post-sum clipping.',
        '- Host-visible controls append after the original 42: `finish` (42), `sub_on_note` (43), `low_cut_track` (44), `loudness_trim` (45), `sub_balance` (46). They are absent from the on-screen editor and randomization locks. Fresh rolls receive voiced defaults; the actual resolved sound receives an offline measured trim. Generated sound settings remain seed-repeatable and serialized.',
        '- Missing old fields restore FINISH 0, octave-down sub, fixed cut, trim 0, sub balance 1. The output default/fresh factory value is 0 dB; an absent historical Output still falls back to −6 dB. Both host state and XML patch loading have explicit migration paths.',
        '- FINISH-enabled fresh sounds report zero host latency as requested. Existing 4× synthesis reconstruction still has its original 24-sample group delay; FINISH adds no delay on engines 3/4. Legacy states preserve their original 0/24-sample reported latency. This distinction is intentional.',
        '- The editor meter already spans −60..0 dBFS. It will show its upper cyan segments more often; its red overload threshold remains 0.99. No meter/glow redesign was made.',
        '- Offline level probes run on message/bounce threads, allocating nothing on the audio thread. A new roll does one 3-second offline probe; factory loading with changed locks can do several. This adds a short generation/load pause; callback CPU below excludes those probes.', '',
        '## Callback CPU','', 'Actual headless VST3 processors, 48 kHz/256 frames, alternating C2/G2 notes, 140 BPM, 8 seconds per trial. One warm-up plus six measured trials, median callback wall time. File I/O, allocations outside callbacks, analysis and preset voicing excluded. Percent is one realtime audio thread.', '',
        '```text', (root/'cpu-final.log').read_text().strip(), '```', '',
        'The earlier `cpu-before.log` is a separate whole-bank console timing (261 seconds of audio); it includes console/analysis overhead and is not the callback comparison above.', '',
        '## Bank medians','', '| Measure | Before | After |','| --- | ---: | ---: |',
        f'| Integrated LUFS | {med(oldl):.3f} | {med(newl):.3f} |',f'| True peak, median dBTP | {med(oldtp):.3f} | {med(newtp):.3f} |',f'| True peak, maximum dBTP | {max(oldtp):.3f} | {max(newtp):.3f} |']
    for i,label in enumerate(['Below 30 Hz','30–60 Hz','60–120 Hz','120–500 Hz','40–120 Hz']):text.append(f'| {label}, RMS dBFS | {med([x["before_bands"][i] for x in spectral]):.3f} | {med([x["after_bands"][i] for x in spectral]):.3f} |')
    text += [f'| Vowel contrast, dB | {vc0:.4f} | {vc1:.4f} |','','## Per-preset results','','Each paired value is **before → after**. Band columns are RMS dBFS; contrast is envelope RMS dB.','', '| Folder / preset | LUFS I | True peak dBTP | 30–60 Hz | 60–120 Hz | 120–500 Hz | Vowel contrast | Change |','| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for x in spectral:
        f=x['file'];name=f.split('-',1)[1][:-4];b=before[f];a=after[f];pair=lambda p,q:f'{p:.2f} → {q:.2f}'
        cells=[f'{folders[name]} / {name}',pair(float(b['LUFS_I']),float(a['LUFS_I'])),pair(float(b['true_peak_dBTP']),float(a['true_peak_dBTP']))]
        cells += [pair(x['before_bands'][i],x['after_bands'][i]) for i in [1,2,3]]
        cells += [pair(x['before_contrast'],x['after_contrast']),f'{x["contrast_change_pct"]:+.2f}%'];text.append('| '+' | '.join(cells)+' |')
    text += ['','## Roll families','','| Family | Seeds | Min / median / max LUFS | Maximum dBTP |','| --- | ---: | ---: | ---: |']
    for family in sorted({x['family'] for x in roll}):
        group=[x for x in roll if x['family']==family];v=vals(group,'LUFS_I');text.append(f'| {family} | {len(group)} | {min(v):.3f} / {med(v):.3f} / {max(v):.3f} | {max(vals(group,"true_peak_dBTP")):.3f} |')
    text += ['','## Tests and iterations','','Every executed test/render mode is listed below, including rejected development results. Logs are retained beside this report. Unknown/GUI modes were never invoked.','', '| Check | Result | Evidence |','| --- | --- | --- |',
        '| Preserved baseline `--render-demos` and `--preset-stats` | 87/87 sane | `before-render.log`, `cpu-before.log` |',
        '| Baseline `--measure-wavs` | 87 metered | `before-measured.tsv` |',
        '| Initial `--bake-finish` | 87 calibrated; intermediate voicing | `bake-01.tsv` |',
        '| FINISH console iteration 1 | 10/13; corrected reference expectation, float comparison, alias ceiling | `check-finish-01.log` |',
        '| FINISH console iteration 2 | 13/13 | `check-finish-02.log` |',
        '| FINISH/processor parity iteration 3 | 16/17; found history-dependent Classic FM phase | `check-finish-03.log` |',
        '| FINISH/processor parity iteration 4 | 17/17, exact sample parity on four probes | `check-finish-04.log` |',
        '| Initial strict 0.2.5 check | Rejected incorrect 0.2.4 reference version; no crash | `check-025-01.log` |',
        '| Strict rebuilt 0.2.5 compatibility, first pass | 36/36, worst sample error 0 | `check-025-02.log` |',
        '| Existing compatibility harness, preserved 0.2.4 | 36/36, worst sample error 0 | `check-compat-024.log` |',
        '| Console-only core, first pass | 22,236 passed, 0 failed | `core-01.log` |',
        '| Initial 200-seed roll run | 200/200; sample peak checked | `rolls-01.tsv` |',
        '| 200-seed default-patch run with true peak | 200/200 | `rolls-default-patch.tsv` |',
        '| Intermediate after renders 1–6 | All 87 sane each; early levels/vowel contrast rejected and revoiced | `after-render-01.log` through `after-render-06.log` |',
        '| Voicing candidates | 63 measured candidates across seven problematic presets | `candidates.tsv`, `candidates-spectral.json` |',
        '| Intermediate bakes 2/3 and first final bake | 87 calibrated each | `bake-02.tsv`, `bake-03.tsv`, `bake-final-before-soft.tsv` |',
        '| First CPU fixture | Discarded wide label: wrong parameter name; mono measured ~1.50× | `cpu-initial.log` |',
        '| Final Release build, all four targets | PASS, no auto-install; signatures verified | `build-final.log` |',
        '| Intermediate WAV metering | 87 files each | `after-measured-04.tsv`, `after-measured-06.tsv` |',
        '| Final factory bake/render/measurement | 87/87 within loudness and true-peak bounds | `bake-final.tsv`, `after-render-final.log`, `after-measured-final.tsv` |',
        '| Final spectral/formant analysis | Median increases; no loss >10% | `spectral.json` |',
        '| Independent ffmpeg cross-check | 174/174 | `ffmpeg-crosscheck.json` |',
        '| Final 200 fresh-default NEW YOI seeds | 200/200 including true peak | `rolls-final.tsv` |',
        '| Artifact format checks | 176 WAV headers and all 12 MP3 durations/320 kb/s streams pass. Initial helper assumptions about ffprobe availability and codec labels were corrected | `artifact-validation.log` |',
        '| Python helper syntax checks and MP3 encoding | PASS | `python3 -m py_compile`; `spectral-final.log`, `ab/` |',
        '| Final callback CPU | See measured ratios above | `cpu-final.log` |']
    for filename,label in [('check-finish-final.log','Final DSP/route/DC/alias/parameter tests'),('core-final.log','Final console-only core'),('check-025-final.log','Final 0.2.5 compatibility, engines 1–4'),('check-compat-final.log','Final existing compatibility harness')]:
        lines=(root/filename).read_text().splitlines();text.append(f'| {label} | {lines[-1]} | `{filename}` |')
    text += ['| Expanded compatibility fixture, first attempt | Raw audio error 0; rejected shifted JUCE-generated MIDI CC indices. Harness now keeps real indices strict and verifies generated MIDI IDs/steps/values separately | `check-025-synthetic-index-failure.log.gz`, `check-compat-synthetic-index-failure.log.gz` |', '| Whitespace/diff review | PASS | `git diff --check -- yoi-boi` |','','## Listening artifacts and remaining external checks','',
        '- `before/` and `after/`: 87 stereo 24-bit/48 kHz WAVs each, plus the 261-second factory tour. `after/YOI-BOI-factory-tour.wav` is the complete final bank.',
        '- `ab/`: **12 MP3 files at 320 kb/s**, containing **18 comparisons**: three presets from each of Yoi/Talk/Growl/Wobble/Sub/Classic. Each family has one single-preset file and one two-preset file, satisfying both the requested 12 files and three examples per family. Every pair is before, one-second silence, after. Natural levels; no loudness matching. `ab/WHAT EACH FILE IS.txt` identifies every sound.',
        '- Claude must still run the unrestricted full suite, GUI/editor/host-key tests, AU host check and pluginval outside this sandbox, then audition the exports in the normal listening environment. No such executable/mode was launched here.',
        '- No known failed audio acceptance criterion remains in these console results. GUI appearance, AU runtime hosting and pluginval remain unverified here. No plug-in was installed while Live was open.']
    (root/'measurements.md').write_text('\n'.join(text)+'\n')
    summary={'factory_count':87,'before_lufs_median':med(oldl),'after_lufs_min':min(newl),'after_lufs_max':max(newl),'after_lufs_median':med(newl),'max_dbtp':max(newtp),'vowel_median_change_percent':100*(vc1/vc0-1),'vowel_worst_change_percent':worst,'roll_min_lufs':min(rolls_l),'roll_max_lufs':max(rolls_l),'roll_max_dbtp':max(rolls_tp),'ffmpeg_max_lu_difference':max(differences)}
    (root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
    if args.docs:
        repo=Path(__file__).resolve().parents[2]
        p=repo/'FACTORY-PRESETS.md';s=p.read_text();s=s.replace('# YOI-BOI 0.2.0 factory bank','# YOI-BOI 0.3.0 factory bank').replace('YOI-BOI 0.2.0 ships','YOI-BOI 0.3.0 ships').replace('on their original engine and unchanged.','on their original engine, with the new backend FINISH voicing. Saved old sessions retain their original output.').replace('Every engine-4 sound uses the 110 Hz **Body low-cut**','Every fresh engine-4 sound uses the note-tracking **Body low-cut** at sqrt(2) × the lowest held note (35–350 Hz)')
        a=s.index('## How the numbers were measured');b=s.index('## The sounds',a)
        s=s[:a]+f'''## How the numbers were measured

The standard console phrase is stereo 48 kHz at 140 BPM, notes 43/46 at velocity 112 over 3 seconds, with wheel/pressure sweeps. The default Length/Sub player locks are retained. When locks change a factory recipe, the resolved sound receives an offline measured trim.

**All 87 sounds:** {min(newl):.3f} to {max(newl):.3f} BS.1770-4 gated integrated LUFS; maximum reconstructed true peak {max(newtp):.3f} dBTP. Output defaults to 0 dB. Per-preset FINISH depth/sub balance and the measured trim are baked by `YoiBoiTests --bake-finish`. `--preset-stats` uses a proper gated meter; `--measure-wavs` measures integrated LUFS and 4× true peak. The former ungated “LUFS-ish” levels are superseded.

FINISH processes the body with program-relative three-band upward/downward compression, sums the protected sub, then applies oversampled GLUE/ceiling and a DC blocker after clipping. Engine 4's sub is on the note, independent of its body pitch envelope. Classic's octave-down pitch and fixed 110 Hz body cut remain. New host-only controls are not added to the editor or randomization locks. Old states/XML files missing the new fields load FINISH off, with zero trim and their original sub/cut behaviour, bit-identically.

Vowel contrast median increases {100*(vc1/vc0-1):.2f}%; the largest individual loss is {-worst:.2f}%. All 200 fresh-default NEW YOI seeds pass −10 ±2 LU and −1 dBTP. Full per-preset band, contrast, CPU and compatibility evidence: `../../outputs/yoi-boi-0.3.0/measurements.md`.

To repeat: `--render-demos <absolute-folder>` writes all demos and the tour; `--measure-wavs <absolute-folder>` meters the WAVs. Use the final Release console binary, not the no-arguments GUI-containing suite, in a sandbox.

'''+s[b:]
        s=s.replace('| LUFS-ish |','| LUFS I |')
        lookup={f.split('-',1)[1][:-4]:v for f,v in after.items()}
        lines=[]
        for line in s.splitlines():
            cells=[x.strip() for x in line.split('|')]
            if len(cells)>=7 and cells[1] in lookup:
                row=lookup[cells[1]];cells[-3]=f'{float(row["true_peak_dBTP"]):.2f}';cells[-2]=f'{float(row["LUFS_I"]):.2f}';line='| '+' | '.join(cells[1:-1])+' |'
            line=line.replace('Peak dBFS','True peak dBTP')
            lines.append(line)
        p.write_text('\n'.join(lines)+'\n')
        p=repo/'VALIDATION.md';s=p.read_text();marker='<!-- 0.3.0 FINISH VALIDATION -->';historical='<!-- 0.2.0 HISTORICAL CONTENT -->'
        if s.startswith(marker):s=s.split(historical,1)[1].lstrip()
        intro=f'''{marker}
# YOI-BOI 0.3.0 console validation — 2026-11-11

All 87 factory sounds measure {min(newl):.3f} to {max(newl):.3f} LUFS integrated, maximum true peak {max(newtp):.3f} dBTP. Vowel/formant contrast median increases {100*(vc1/vc0-1):.2f}%; no preset loses more than {-worst:.2f}%. All 200 fresh-default rolls pass −10 ±2 LU and −1 dBTP.

FINISH, tracked low cut, on-note sub, compatibility migration and measured trims are covered by console-only modes. The full results, callback CPU, exact old-state checks and every executed test/iteration are recorded in `../../outputs/yoi-boi-0.3.0/measurements.md`. The Release AU/VST3/Standalone/test targets build with `COPY_PLUGIN_AFTER_BUILD` OFF. No plug-in installation, GUI/window/editor construction, AU hosting, Standalone execution or pluginval occurred in this sandbox.

The pre-existing executable was actually 0.2.4; it was preserved before rebuilding. A 0.2.5 reference was built from e11fd98 for strict compatibility. New FINISH-enabled sounds report zero latency; legacy states retain their original 0/24-sample latency and exact rendering. The existing 24-sample reconstruction delay remains in engines 3/4; no extra delay was added there.

Claude still needs to run the unrestricted full suite, GUI/editor and host-key tests, AU host check, and pluginval outside this sandbox. The meter keeps its existing −60..0 dB scale and will occupy more upper cyan segments.

---

## Historical 0.2.0 validation (not rerun as a full suite for 0.3.0)

The results below are historical. Their old ungated level claims and “unchanged factory output” statements are superseded by the 0.3.0 measurements above. Their GUI/pluginval/AU successes are not claims about this sandbox run.

'''
        p.write_text(intro+historical+'\n\n'+s)
if __name__=='__main__':main()
