#!/usr/bin/env python3
"""Reproducible WAV spectral analysis and natural-level MP3 A/B assembly.
BS.1770/true peak comes from the console --measure-wavs outputs, cross-checked
with ffmpeg ebur128. This script uses only Python + NumPy already on the host.
"""
import argparse, os, shutil, csv, json, wave, subprocess
from pathlib import Path
import numpy as np

def read(path):
    with wave.open(str(path),'rb') as w:
        assert w.getframerate()==48000 and w.getnchannels()==2
        b=w.readframes(w.getnframes()); width=w.getsampwidth()
    if width==3:
        z=np.frombuffer(b,np.uint8).reshape(-1,3).astype(np.int32)
        x=z[:,0]+(z[:,1]<<8)+(z[:,2]<<16); x=np.where(x & 0x800000,x-0x1000000,x)/8388608
    elif width==2: x=np.frombuffer(b,'<i2')/32768
    else: raise ValueError(width)
    return x.reshape(-1,2)

def spectral(x):
    # Whole-render band RMS by Parseval, stereo power mean, dBFS.
    n=len(x); power=np.mean(np.abs(np.fft.rfft(x,axis=0))**2,axis=1)*2/n**2
    f=np.fft.rfftfreq(n,1/48000)
    bands=[10*np.log10(max(np.sum(power[(f>=a)&(f<b)]),1e-15)) for a,b in [(0,30),(30,60),(60,120),(120,500),(40,120)]]
    # Speech envelope: 85 ms Hann frames, 50% hop, 1/6-octave energy bands,
    # 300-4000 Hz. Remove a linear tilt in log frequency, then RMS residual
    # dB. This is level-independent peak/valley formant contrast, not a
    # classifier, F0 score, or listening-test claim. Only active note frames.
    size=4096; hop=2048; starts=np.arange(0, int(1.86*48000)-size,hop)
    windows=np.stack([x[i:i+size] for i in starts]); window=np.hanning(size)
    p=np.mean(np.abs(np.fft.rfft(windows*window[None,:,None],axis=1))**2,axis=2)
    freq=np.fft.rfftfreq(size,1/48000)
    centres=300*2**(np.arange(24)/6); centres=centres[centres<=4000]
    env=np.stack([np.mean(p[:,(freq>=c/2**(1/12))&(freq<c*2**(1/12))],axis=1) for c in centres],axis=1)
    env=10*np.log10(np.maximum(env,1e-15))
    design=np.column_stack([np.ones(len(centres)),np.log2(centres)])
    residual=env-env@np.linalg.pinv(design).T@design.T
    active=np.array([any(a<= (i+size/2)/48000 <b for a,b in [(0,.38),(.57,.77),(.86,1.07),(1.28,1.86)]) for i in starts])
    contrast=float(np.median(np.sqrt(np.mean(residual[active]**2,axis=1))))
    return bands,contrast

def main():
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--ab',action='store_true');args=ap.parse_args();root=args.output
    results=[]
    for f in sorted((root/'before').glob('[0-9]*.wav')):
        other=root/'after'/f.name
        if not other.exists(): continue
        a,ac=spectral(read(f));b,bc=spectral(read(other))
        results.append({'file':f.name,'before_bands':a,'after_bands':b,'before_contrast':ac,'after_contrast':bc,'contrast_change_pct':100*(bc/ac-1)})
    (root/'spectral.json').write_text(json.dumps(results,indent=2)+'\n')
    if results:
        print('contrast medians',np.median([v['before_contrast'] for v in results]),np.median([v['after_contrast'] for v in results]),'worst',min(v['contrast_change_pct'] for v in results))
        for v in sorted(results,key=lambda v:v['contrast_change_pct'])[:12]:print(v['file'],round(v['contrast_change_pct'],2))
    if args.ab:
        ffmpeg=os.environ.get('FFMPEG') or shutil.which('ffmpeg') or 'ffmpeg'  # set FFMPEG=/path/to/ffmpeg if it isn't on PATH
        # 12 files, two per requested family, three unique presets/family:
        # first file one preset, second file two presets. Each preset is its
        # own BEFORE / 1 s gap / AFTER pair (18 comparisons in 12 files).
        groups={'Yoi':['Tape Bounce II','Chrome Chew II','Yoi Boi Prime'], 'Talk':['Chatterbox','Say What','Back Talk'],
                'Growl':['Grizzly','Beast Mode','Fang Fold'], 'Wobble':['Wub Lord','Triplet Womp','Wobble Bell'],
                'Sub':['Clean Sub Talk','808 Yoi','Subterranean'], 'Classic':['Tape Bounce','Chrome Chew','Squeaky Clean']}
        (root/'ab').mkdir(exist_ok=True);notes=['Natural-level comparisons. No loudness matching, limiting, or normalization in assembly.','Each named sound: 3 s BEFORE, 1 s silence, 3 s AFTER. Two-preset files add 1 s silence between pairs.','87 factory WAVs and full tour retain their original rendered levels.','']
        files=list((root/'before').glob('[0-9]*.wav'))
        for family,names in groups.items():
            for index,selection in enumerate([names[:1],names[1:]],1):
                pieces=[]
                for name in selection:
                    f=next(f for f in files if f.stem.split('-',1)[1]==name)
                    if pieces:pieces.append(np.zeros((48000,2)))
                    pieces.extend([read(f),np.zeros((48000,2)),read(root/'after'/f.name)])
                data=np.concatenate(pieces).astype('<f4')
                dest=root/'ab'/f'{family}-{index}.mp3'
                subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-f','f32le','-ar','48000','-ac','2','-i','pipe:0','-codec:a','libmp3lame','-b:a','320k',str(dest)],input=data.tobytes(),check=True)
                notes.append(f'{dest.name}: '+', then '.join(selection)+'.')
        (root/'ab'/'WHAT EACH FILE IS.txt').write_text('\n'.join(notes)+'\n')
if __name__=='__main__':main()
