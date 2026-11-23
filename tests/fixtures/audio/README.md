# Audio fixtures

Synthetic tones only (no recordings): 0.5 s, 44.1 kHz stereo, 440 Hz left and 660 Hz right,
22050 frames. Used by `tst_audio_decoder` and `tst_audio_engine`.

`long.mp3`: 4 s, 22.05 kHz mono, 330 Hz, long enough for a transition (`tst_audio_engine`).

`fade.mp3`: 8.5 s, 22.05 kHz mono, 550 Hz: 0.5 s of silence, 4 s of tone, a fade-out of
17 dB a second for 3 s, silence (a natural end, `tst_audio_engine`).

`tone_aac.m4a`, `tone_fmp4.m4a`, `tone_eac3.m4a`: the same 0.5 s tone as MP4 audio, for the
decoder's MP4 container paths (`tst_audio_decoder`). `tone_aac.m4a` has its `moov` after the
`mdat`; `tone_fmp4.m4a` is fragmented (moof/mdat, no trailing moov); `tone_eac3.m4a` is E-AC-3.
The codec is named by `format` (`aac`/`eac3`), never by the file suffix.

```bash
ffmpeg -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)|0.5*sin(2*PI*660*t):s=44100:d=0.5" -c:a pcm_s16le tone.wav
lame -b 128 tone.wav tone.mp3
flac -8 tone.wav -o tone.flac
ffmpeg -i tone.wav -c:a aac -b:a 128k -f mp4 tone_aac.m4a
ffmpeg -i tone.wav -c:a aac -b:a 128k -movflags frag_keyframe+empty_moov+default_base_moof -f mp4 tone_fmp4.m4a
ffmpeg -i tone.wav -c:a eac3 -b:a 192k -f mp4 tone_eac3.m4a
ffmpeg -f lavfi -i "aevalsrc=0.3*sin(2*PI*330*t):s=22050:d=4" -c:a pcm_s16le long.wav
lame -b 32 long.wav long.mp3
ffmpeg -f lavfi -i "aevalsrc='if(lt(t,0.5),0,if(lt(t,4.5),0.3*sin(2*PI*550*t),if(lt(t,7.5),0.3*pow(10,-(t-4.5)*17/20)*sin(2*PI*550*t),0)))':s=22050:d=8.5" -c:a pcm_s16le fade.wav
lame -b 32 fade.wav fade.mp3
```
